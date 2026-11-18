# PolyExecution Studio (PES)

PolyExecution Studio is a collaborative multi-language code execution platform under development. The planned architecture lets multiple users collaboratively edit code in a browser and submit runs through an ingress gateway to backend worker services that compile and execute user code inside a C sandbox runner constrained by Linux namespaces, cgroups v2, a private tmpfs rootfs, capability dropping, and seccomp-bpf.

---

## Current Implementation State (Phase 7)

The project currently implements the full collaborative execution stack:

1. **C Sandbox Runner (`sandbox-daemon/sandbox-runner`)**:
   - Systems programming core implementing low-level process lifecycle management (`fork`, `execve`, `wait4`), resource limit enforcement (`RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_FSIZE`, `RLIMIT_CORE`), and non-blocking stream capture (`poll`, pipe draining).
   - Linux security hardening implementing cgroups v2 (`pids.max = 32`, CPU time, memory limits), Linux namespaces (User, Mount, PID, Network), a minimal tmpfs rootfs with `pivot_root`, capability bounding set pruning, `PR_SET_NO_NEW_PRIVS`, and a strict seccomp-bpf allowlist.
   - Fail-closed isolation: if any prerequisite isolation mechanism cannot be initialized, the runner terminates immediately with a structured setup error and zero target execution.

2. **Kotlin gRPC Execution Worker (`worker/`)**:
   - A Kotlin coroutine service implementing `pes.execution.v1.ExecutionService` over gRPC and HTTP/2.
   - Additive progress streaming contract (`ExecuteProgressStream`) emitting truthful milestone events: `STAGE_COMPILING` before sandboxed GCC execution, and `STAGE_RUNNING` before sandboxed target execution (or Python execution). Invalid C emits `COMPILE_ERROR` immediately and halts without emitting a running milestone.
   - Bounded request validation (job ID regex, resource limits, source code size ceiling of 256 KiB, standard input ceiling of 64 KiB).
   - Ephemeral workspace lifecycle: creates private directories (`/tmp/pes-job-<id>-*` with mode `0700`), writes source files, and guarantees recursive unlinking in a `finally` block with validation against symlinks and path traversal.
   - Sandboxed compilation: routes `/usr/bin/gcc` through `sandbox-runner` with pre-opened descriptor handoff for the output artifact.
   - Sandboxed execution: routes compiled targets or `/usr/bin/python3` through `sandbox-runner` with bounded standard input forwarding.
   - High-concurrency I/O: uses dedicated Java 21 virtual threads (`Executors.newVirtualThreadPerTaskExecutor()`) for asynchronous process pipe draining and standard input feeding, avoiding thread starvation under heavy concurrent execution loads.
   - Result classification: translates runner results into typed protobuf statuses (`OK`, `COMPILE_ERROR`, `RUNTIME_ERROR`, `TIMEOUT`, `OUTPUT_LIMIT`, `MEMORY_LIMIT`, `SANDBOX_ERROR`).

3. **Java Spring Boot Gateway (`gateway/`)**:
   - Ingress gateway binding strictly to loopback (`127.0.0.1:8080`) providing a WebSocket endpoint on `/ws` and a REST health check on `/health`.
   - Structured JSON protocol: handles client messages (`room.join`, `room.leave`, `run`, `collab.update`, `collab.awareness`, `collab.sync`) and emits truthful server events (`room.joined`, `room.peer_joined`, `room.left`, `run.accepted`, `run.progress`, `run.rejected`, `run.completed`, `collab.update`, `collab.awareness`, `collab.sync`, `error`).
   - Progress relay: forwards worker progress stream milestones as `{ "type": "run.progress", "roomId": "...", "requestId": "...", "stage": "COMPILING" | "RUNNING" }` exclusively to members of the room.
   - Real-time collaboration relay: routes opaque Yjs CRDT updates (`collab.update`) and ephemeral awareness deltas (`collab.awareness`) exclusively to connected peers in the same room. Enforces bounds: max 64 KiB per collab update, max 16 KiB per awareness update, 120 collab updates/sec rate limit per session, and bounded in-memory room history (up to 2,000 updates / 2 MiB) for initial synchronization of newly joined clients (`collab.sync`).
   - Bounded in-memory room manager: enforces room capacity (max 100 rooms, max 20 sessions per room), sliding-window rate limiting (30 runs/minute), thread-safe session tracking, disconnect cleanup, and strict room isolation with zero cross-room event leakage.
   - Concurrency governance: enforces an explicit single active run per room policy and configurable global concurrent execution ceiling (`gateway.max-global-concurrent-runs`, default 16, scalable to 64+).
   - Origin security: explicit development origin allowlist rejecting cross-site WebSocket hijacking attempts.

4. **React Monaco Web Client with Yjs Collaboration (`client/`)**:
   - Real-time collaborative workbench built with React 18, TypeScript, Vite, Monaco Editor, Yjs (`yjs`), Awareness (`y-protocols`), and Monaco Binding (`y-monaco`).
   - Shared CRDT document: room-scoped `Y.Doc` with `Y.Text` ('monaco') bound to Monaco Editor via `MonacoBinding`, synchronizing edits peer-to-peer through the Gateway with deterministic conflict-free convergence.
   - Ephemeral presence: renders remote collaborators' colored cursor head indicators and selection highlights in Monaco, with collaborator pill badges in the header, and immediate removal upon participant disconnect (`room.left`).
   - Room routing: supports URL query param (`?room=<id>`) and room join input bar for switching and sharing rooms.
   - Multi-language support: interactive language selector for C, Python, Java, Kotlin, and Node.js with shared language synchronization across room peers and default starter templates.
   - Standard input panel: user-configurable stdin textarea forwarded to the sandbox process.
   - Truthful execution lifecycle state badge rendering: `Idle`, `Accepted`, `Compiling`, `Running`, `Completed`, `Timed Out`, and `Compile Error`.
   - Stream separation: independent, bounded viewers for standard output (stdout) and standard error (stderr).
   - Real-time telemetry panel: renders truthful metrics reported by the Linux sandbox runner (`wallMs`, `cpuMs`, `peakMemoryKb`, `exitCode`, `signal`, `status`).

5. **Reproducible Docker Compose & Load Benchmark (`docker-compose.yml`, `gateway/tests/`)**:
   - Containerization: Multi-stage Dockerfiles for Gateway (Eclipse Temurin JRE 21), Worker (Ubuntu 24.04 with GCC, Python 3, OpenJDK 21, Kotlin 2.1.0, Node.js 20+, libseccomp, and native sandbox runner), and Client (Node 22 build + Nginx Alpine static server and reverse proxy).
   - Network topology: Private internal bridge network (`pes-net`) with host ports strictly bound to loopback (`127.0.0.1:8080`, `127.0.0.1:50051`, `127.0.0.1:5173`).
   - Verified 50 concurrent runs load benchmark: Automated harness (`gateway/tests/verify_load_test_50.sh`) opens 50 concurrent WebSocket sessions across 50 distinct isolated rooms, dispatches 50 simultaneous code executions (mixed Java, C, and Python) through the real Gateway -> Worker -> Sandbox Runner pipeline, and completes in 8,989 ms (100% success; Min: 3,352 ms, Median: 3,622 ms, p95: 6,242 ms, Max: 6,466 ms) with zero lingering cgroups and zero stray processes.
   - Truthful overload protection: Verified honest `ROOM_RUN_IN_PROGRESS` rejection when room concurrency is exceeded.

---

## Roadmap & Phase Status

| Phase | Component | Status | Definition of Done |
|---|---|---|---|
| **Phase 0** | Repo Setup & Governance | Complete (`phase-0-done`) | Pre-commit hook rejects edits to old progress log entries |
| **Phase 1** | C Sandbox v1 (Process Lifecycle & Limits) | Complete (`phase-1-done`) | Correct execution and statuses for clean exit, exit codes, CPU/wall timeouts, memory bomb, output flood; no zombies |
| **Phase 2** | C Sandbox v2 (Linux Isolation & Defences) | Complete (`phase-2-done`) | Fork bomb containment (`pids.max`), filesystem snooping defense, network denial via seccomp; cgroup cleanup verified; 0 stray processes |
| **Phase 3** | Kotlin Worker & Sandboxed Compilation | Complete (`phase-3-done`) | C and Python run end-to-end from a gRPC client (`grpcurl`), with C compilation running inside the sandbox |
| **Phase 4** | Java Gateway & WebSocket Server | Complete (`phase-4-done`) | WebSocket client triggers run and receives status events |
| **Phase 5** | Browser Web Client (Monaco Editor) | Complete (`phase-5-done`) | Browser editor executes code; renders Idle, Compiling, Running, Timed Out states |
| **Phase 6** | Real-Time Collaboration (Yjs CRDT) | Complete (`phase-6-done`) | Two browsers converge; cursors visible; room isolation; disconnect cleanup |
| **Phase 7** | Multi-Language Runtimes, Docker Compose, & Load Benchmarks | In Progress | Java/Kotlin/Node support, Docker Compose, 50 concurrent runs load benchmark, documented limits; tutor review and owner learning gate in docs/05_INTERVIEW_PREP.md pending |

---

## Honest Limitations

In accordance with project governance rules, the following limitations reflect current functionality:

- **Loopback binding only**: All services (client at `127.0.0.1:5173`, gateway at `127.0.0.1:8080`, and worker at `127.0.0.1:50051`) bind strictly to loopback. Public ingress, TLS termination, and external identity provider authentication are deferred.
- **Real-time collaboration**: Concurrent multi-user collaborative editing is implemented using Yjs CRDT (`yjs`), Monaco binding (`y-monaco`), and ephemeral presence (`y-protocols` Awareness). Edits converge deterministically without centralized locks. Updates are buffered up to 2 MiB / 2,000 updates per room, and rate-limited to 120 collab updates/sec per session.
- **In-memory room state**: Collaboration rooms, update history, and session memberships are stored entirely in JVM memory and are not persisted across server restarts. Empty rooms are pruned upon disconnection.
- **Truthful milestone events (no fake timers)**: Progress states (`Compiling`, `Running`) reflect actual worker milestone emissions immediately before sandboxed GCC compilation and before sandboxed execution. Compilation failures immediately halt and emit `Compile Error` without entering the running state.
- **Unauthenticated sessions**: WebSocket sessions use server-assigned random UUIDs. Client-supplied user identifiers are untrusted and used only for room display metadata.
- **Language support & runtime limits**: The Gateway, Worker pipeline, and web client support C, Python 3, Java, Kotlin, and Node.js:
  - C (`source.c`) is compiled via `/usr/bin/gcc -O2` inside the sandbox under `STAGE_COMPILING` to an isolated binary `target`, which is executed under `STAGE_RUNNING`. Effective peak compile memory is 512 MiB.
  - Python 3 (`source.py`) is syntax-checked via `python3 -m py_compile` inside the sandbox under `STAGE_COMPILING`. If syntax check passes, it is executed via `/usr/bin/python3` under `STAGE_RUNNING`. Effective peak memory is 512 MiB.
  - Java (`source.java` / `Main.java`) uses a truthful two-stage lifecycle: Stage 1 compiles via `javac` under `STAGE_COMPILING` to `/workspace/<ClassName>.class` and exports the class artifact; Stage 2 executes bytecode via `java -cp /workspace <ClassName>` directly without source recompilation. Effective peak compile memory is 3072 MiB.
  - Kotlin (`source.kt`) uses a truthful two-stage lifecycle: Stage 1 compiles via OpenJDK `org.jetbrains.kotlin.cli.jvm.K2JVMCompiler` under `STAGE_COMPILING` to `/workspace/target.jar` and exports the jar artifact; Stage 2 executes bytecode via `java -jar /workspace/target.jar` directly without source recompilation. Effective peak compile memory is 3072 MiB.
  - Node.js (`source.js`) validates syntax via `node --check` under `STAGE_COMPILING` and executes via `node /workspace/source.js` under `STAGE_RUNNING` with 5120 MiB virtual memory allowance for the V8 pointer compression cage. Effective peak memory is 4096 MiB.
  - Non-networking event-loop syscalls (`epoll_create1`, `epoll_ctl`, `epoll_wait`, `eventfd2`) are permitted in the seccomp allowlist, while socket syscalls (`socket`, `connect`, `bind`) remain strictly denied with `EPERM`. `io_uring_setup`, `io_uring_enter`, and `io_uring_register` return `ENOSYS` to enable libuv graceful fallback to epoll.
  - Seccomp restricts `prctl` operations strictly to `PR_SET_NAME` and `PR_GET_NAME` (required for JVM thread naming); all unauthorized `prctl` operations are terminated immediately via `SIGSYS`.
- **Aggregate memory admission control**: Gateway computes effective per-language peak memory (Java/Kotlin: 3072 MiB, Node: 4096 MiB, Python/C: 512 MiB) and tracks total memory against `gateway.max-aggregate-memory-mb` (default 8192 MiB). Excess runs are rejected truthfully with `GLOBAL_MEMORY_LIMIT_REACHED` until active runs complete or timeout.
- **Docker Compose status**: Worker gRPC service binds to `0.0.0.0` inside container via `PES_WORKER_HOST=0.0.0.0` (defaulting to `127.0.0.1` locally), allowing the Gateway container to communicate across the bridge network while host port publication remains strictly loopback-bound (`127.0.0.1:50051`). Docker Compose configuration is verified via `docker compose config`. Live multi-container execution inside Docker Compose remains `UNVERIFIED` in environments where the host Docker daemon socket (`/var/run/docker.sock`) is not mounted.
- **Linux host requirements**: Production sandbox execution requires a Linux kernel with cgroups v2 delegation (`cpu`, `memory`, `pids`) and unprivileged user namespaces. If these prerequisites are absent, the runner strictly fails closed (`SANDBOX_ERROR`).
- **Containment model**: Isolation is enforced via Linux kernel namespaces, cgroups, and seccomp-bpf. It does not use hypervisor-level virtual machines (such as Firecracker or gVisor).

---

## Quickstart & Verification

### Prerequisites
- Linux host with cgroups v2 enabled and unprivileged user namespaces
- GCC, Make, libseccomp (`libseccomp-dev` or `libseccomp`)
- JDK 21+ and Python 3.10+
- `grpcurl` (for gRPC verification)

### 1. Build and Test the C Sandbox Runner
```bash
# Build sandbox runner and adversarial test fixtures
make -C sandbox-daemon clean all evil

# Run the 23-test integration suite (18 Phase 1 + 5 Phase 2 checks)
make -C sandbox-daemon test

# Run safe unit and structural tests
make -C sandbox-daemon test-workspace-handoff test-setup-protocol test-phase2-structural test-phase2-seccomp-safe test-phase2-fail-closed
```

### 2. Build and Test the Kotlin Worker
```bash
# Run worker unit tests (from repository root)
(cd worker && gradle test)

# Build the worker distribution (from repository root)
(cd worker && gradle installDist)
```

### 3. Build and Test the Java Gateway
```bash
# Run gateway unit and in-process gRPC integration tests (from repository root)
(cd gateway && gradle test)

# Build the gateway executable jar (from repository root)
(cd gateway && gradle bootJar)
```

### 4. Build and Test the React Monaco Client
```bash
# Run client unit and component tests (from repository root)
(cd client && npm test)

# Build the production frontend distribution (from repository root)
(cd client && npm run build)
```

### 5. Browser End-to-End Verification (Phases 5 & 6)
Run the complete collaborative vertical slice (Chromium Browser pages -> Vite -> WebSocket Gateway -> Kotlin Worker -> Sandbox Runner):

```bash
# Run the automated Playwright browser test suite (collab convergence + execution)
bash client/tests/verify_e2e_browser.sh 8080 50051 5173
```

### 6. Phase 4 End-to-End WebSocket Verification
Run the Java gateway to Kotlin worker verification:

```bash
# Verify end-to-end WebSocket execution with real sandboxed C and Python
bash gateway/tests/verify_e2e_websocket.sh 8080 50055
```

### 7. Worker gRPC Direct Verification via `grpcurl`
```bash
# Run live sandbox compilation and execution checks against worker
bash worker/tests/verify_grpcurl.sh 50055 production-runtime

# Run protocol-only verification with the fake runner
bash worker/tests/verify_grpcurl.sh 50055 fake

# Run fail-closed verification (verifies sandbox error when unprivileged)
bash worker/tests/verify_grpcurl.sh 50055 production
```

### 8. Phase 7 50-Concurrent-Runs Load Benchmark
Run the automated multi-room concurrent execution benchmark:

```bash
# Run 50 simultaneous executions across 50 rooms through Gateway -> Worker -> Sandbox Runner
bash gateway/tests/verify_load_test_50.sh 8080 50051 50
```

### 9. Docker Compose Setup & Validation
```bash
# Validate Compose configuration
docker compose config

# Build and start services (requires host Docker daemon access and cgroups v2)
docker compose up --build -d

# Stop and tear down services
docker compose down
```

---

## Repository Documentation

- [docs/01_PROJECT_OVERVIEW.md](docs/01_PROJECT_OVERVIEW.md): High-level system goals and architectural motivation.
- [docs/02_ARCHITECTURE.md](docs/02_ARCHITECTURE.md): Service boundaries, protocol contracts, and data flow.
- [docs/03_SANDBOX_DEEP_DIVE.md](docs/03_SANDBOX_DEEP_DIVE.md): Detailed Linux systems programming and isolation mechanics.
- [docs/04_BUILD_PLAN.md](docs/04_BUILD_PLAN.md): Phase-by-phase Definitions of Done and dependency rules.
- [docs/05_INTERVIEW_PREP.md](docs/05_INTERVIEW_PREP.md): Systems engineering questions, trade-offs, and explain-back checklist.
- [docs/06_LEARNING_RULES.md](docs/06_LEARNING_RULES.md): Pedagogical rules, honest AI statements, and learning sign-off criteria.
- [PROGRESS.md](PROGRESS.md): Append-only chronological audit log of every change, reason, and test evidence.
- [AI_RULES.md](AI_RULES.md): Non-negotiable repository engineering rules and coding standards.
