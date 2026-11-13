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

## Failure modes to design for
Worker crashes mid-job; sandbox hangs; client disconnects during run; two Runs in one room at once; output of 1 GB; compile bombs (huge templates / macros).
