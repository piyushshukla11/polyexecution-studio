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
Strings in `ExecuteResponse` (`stdout`, `stderr`) follow RFC 8259 JSON encoding:
- Valid multi-byte UTF-8 sequences (2-byte, 3-byte, and 4-byte up to U+10FFFF per RFC 3629) pass through directly.
- ASCII control characters (`< 0x20` and `0x7F`) and raw non-UTF-8 bytes (`0x80..0xFF`, including overlong encodings, surrogates, isolated continuation bytes, and lead bytes above `0xF4`) are escaped as `\u00XX` (Unicode Latin-1 Supplement code points `U+0000`..`U+00FF`).
- **Lossless Byte Reconstruction:** Any client can reconstruct the exact raw byte stream:
  ```python
  def reconstruct_bytes(text: str) -> bytes:
      out = bytearray()
      for char in text:
          cp = ord(char)
          if cp <= 255:
              out.append(cp)
          else:
              out.extend(char.encode("utf-8"))
      return bytes(out)
  ```


## Failure modes to design for
Worker crashes mid-job; sandbox hangs; client disconnects during run; two Runs in one room at once; output of 1 GB; compile bombs (huge templates / macros).
