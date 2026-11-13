# 02 — Architecture & Decisions

```
Browser (React + Monaco + Yjs)
   │  WebSocket (Yjs updates + run requests + status events)
   ▼
Java Gateway (Spring Boot)       ← rooms, sessions, validation, rate limits
   │  gRPC (HTTP/2, protobuf)
   ▼
Kotlin Worker (coroutines)       ← job queue, temp workspace, compile step
   │  spawns process, args + exit code + JSON on stdout
   ▼
C sandbox-runner                 ← fork/exec, rlimits, pipes, timeouts, (later) seccomp/namespaces/cgroups
   │
   ▼
User program (untrusted)
```

## Fixes to the original README (decided in tutoring)
| Issue in original | Decision |
|---|---|
| Diagram said HTTP for "Run", flow said WebSocket | **One channel: WebSocket** for run requests and live status. REST only for health checks. |
| Hardening (namespaces/cgroups) was Phase 5 | Moved to **Phase 2**. `setrlimit` limits resources; it does not isolate. |
| Compilers run unsandboxed | Compile step also runs through the sandbox (compilers are attack surface). |
| "Low-latency" claim | Honest claim: **low overhead per run**. JVM start + compile dominates; we measure it. |
| Java gateway + Kotlin worker | Kept deliberately as a learning choice, and documented as a trade-off (see below). |

## Key decisions and the "why" (interview gold)
1. **C for the sandbox.** Needs direct syscalls (`fork`, `execve`, `setrlimit`, `wait4`, `seccomp`, `clone`). No runtime that hides them. Tiny binary, fast startup.
2. **gRPC between Java and Kotlin.** Typed contract in `execution.proto`, binary, streaming possible, codegen for both sides. Trade-off: more setup than plain HTTP/JSON.
3. **Kotlin coroutines for the worker.** Many jobs wait on I/O (process, files); coroutines handle this without a thread per job. Trade-off: blocking calls must go on `Dispatchers.IO`.
4. **Yjs (CRDT) for collaboration.** Merges concurrent edits without a central lock. We integrate it, not reinvent it.
5. **Separate processes, not threads, for user code.** A crashing or malicious program must not share memory with the worker.
6. **Trade-off to be honest about:** Java gateway + Kotlin worker is extra JVM overhead for one hop. A single Kotlin service could do both. We keep two services to practise service boundaries.

## Data contract (initial `execution.proto` shape)
- `ExecuteRequest { job_id, language, source_code, stdin, limits{cpu_ms, wall_ms, memory_mb, max_output_kb} }`
- `ExecuteResponse { job_id, status(OK|COMPILE_ERROR|RUNTIME_ERROR|TIMEOUT|MEMORY_LIMIT|OUTPUT_LIMIT|SANDBOX_ERROR), stdout, stderr, exit_code, signal, wall_ms, cpu_ms, peak_memory_kb }`

### Status and Telemetry Semantics
1. **TIMEOUT**: Covers both wall-clock deadline expiration (`wall_ms >= timeout_ms`) and CPU resource limit exhaustion via `RLIMIT_CPU` / `SIGXCPU` (`signal: 24`, `cpu_ms >= cpu_limit_ms`). The telemetry fields `signal`, `wall_ms`, and `cpu_ms` allow clients to distinguish the two timeout causes without altering the JSON schema:
   - CPU timeout: `signal == 24` (`SIGXCPU`), `cpu_ms` reaches soft/hard limit.
   - Wall timeout: runner sends `SIGKILL` (or reaps with exit/signal 0), `wall_ms >= timeout_ms`.
2. **MEMORY_LIMIT**: In the Phase 1 POSIX sandbox, `setrlimit(RLIMIT_AS)` causes `malloc` or `mmap` to fail with NULL. Applications typically dereference NULL and terminate with `SIGSEGV` (signal 11). If peak RSS reaches $\ge 80\%$ of `rlimit_as_bytes`, the runner classifies the event as `MEMORY_LIMIT` using an explicit resident-memory heuristic. Note: POSIX `setrlimit` does not deliver a dedicated kernel OOM signal (unlike Linux cgroups v2 OOM killer in Phase 2); `SIGSEGV` without reaching the memory heuristic threshold remains classified as `RUNTIME_ERROR`.
3. **OUTPUT_LIMIT**: Covers stdout/stderr stream cap breach (runner kills process group upon reaching `max_output_bytes`) or file size ceiling breach via `RLIMIT_FSIZE` (`signal: 25` / `SIGXFSZ`).
4. **RUNTIME_ERROR**: Target terminated with an uncaught signal (e.g. `SIGSEGV` not matching memory heuristic, `SIGABRT`, `SIGFPE`) or returned a non-zero exit code.
5. **SANDBOX_ERROR**: Runner initialization error, setup error (e.g. binary not found, execve error), setup pipe protocol error, or lost child process.

### Stream Encoding & Byte Reconstruction Protocol
Strings in `ExecuteResponse` (`stdout`, `stderr`) follow RFC 8259 JSON encoding using a byte-oriented Latin-1 representation:
- Printable ASCII characters (`0x20 <= byte <= 0x7E`, except `"` and `\`) pass through as literal ASCII characters.
- Standard JSON escape sequences are used for quotes (`\"`), backslashes (`\\`), and control escapes (`\b`, `\f`, `\n`, `\r`, `\t`).
- All control characters (`< 0x20` and `0x7F`) and all bytes `>= 0x80` are escaped as `\u00XX` (Unicode Latin-1 Supplement code points `U+0000`..`U+00FF`).
- **Why this design?** Emitting all bytes `>= 0x80` as `\u00XX` guarantees that valid UTF-8 sequences (e.g. `0xC3 0xA9` for `é`) never collide with raw isolated bytes (e.g. `0xE9`). `0xC3 0xA9` emits `\u00c3\u00a9` (two characters: `U+00C3` and `U+00A9`), whereas raw byte `0xE9` emits `\u00e9` (one character: `U+00E9`).
- **100% Pure ASCII JSON:** The JSON output contains only 7-bit ASCII characters, making it universally valid JSON that parses cleanly across every language and runtime without UTF-8 decode errors.
- **Lossless Byte Reconstruction:** Any client reconstructs the original raw binary stream losslessly:
  ```python
  def reconstruct_bytes(text: str) -> bytes:
      return text.encode("latin1")
      # or equivalently: bytes(ord(c) for c in text)
  ```


## Failure modes to design for
Worker crashes mid-job; sandbox hangs; client disconnects during run; two Runs in one room at once; output of 1 GB; compile bombs (huge templates / macros).
