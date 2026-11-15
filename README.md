# PolyExecution Studio (PES)

PolyExecution Studio is a collaborative multi-language code execution platform under development. The planned architecture lets multiple users collaboratively edit code in a browser and submit runs through an ingress gateway to backend worker services that compile and execute user code inside a C sandbox runner constrained by Linux namespaces, cgroups v2, a private tmpfs rootfs, capability dropping, and seccomp-bpf.

---

## Current Implementation State (Phase 4)

The project currently implements the bottom four layers of the architecture:

1. **C Sandbox Runner (`sandbox-daemon/sandbox-runner`)**:
   - Systems programming core implementing low-level process lifecycle management (`fork`, `execve`, `wait4`), resource limit enforcement (`RLIMIT_CPU`, `RLIMIT_AS`, `RLIMIT_FSIZE`, `RLIMIT_CORE`), and non-blocking stream capture (`poll`, pipe draining).
   - Linux security hardening implementing cgroups v2 (`pids.max = 32`, CPU time, memory limits), Linux namespaces (User, Mount, PID, Network), a minimal tmpfs rootfs with `pivot_root`, capability bounding set pruning, `PR_SET_NO_NEW_PRIVS`, and a strict seccomp-bpf allowlist.
   - Fail-closed isolation: if any prerequisite isolation mechanism cannot be initialized, the runner terminates immediately with a structured setup error and zero target execution.

2. **Kotlin gRPC Execution Worker (`worker/`)**:
   - A Kotlin coroutine service implementing `pes.execution.v1.ExecutionService` over gRPC and HTTP/2.
   - Bounded request validation (job ID regex, resource limits, source code size ceiling of 256 KiB, standard input ceiling of 64 KiB).
   - Ephemeral workspace lifecycle: creates private directories (`/tmp/pes-job-<id>-*` with mode `0700`), writes source files, and guarantees recursive unlinking in a `finally` block with validation against symlinks and path traversal.
   - Sandboxed compilation: routes `/usr/bin/gcc` through `sandbox-runner` with pre-opened descriptor handoff for the output artifact. Invalid source returns `COMPILE_ERROR` with compiler diagnostics and halts before execution.
   - Sandboxed execution: routes compiled targets or `/usr/bin/python3` through `sandbox-runner` with bounded standard input forwarding.
   - Result classification: translates runner results into typed protobuf statuses (`OK`, `COMPILE_ERROR`, `RUNTIME_ERROR`, `TIMEOUT`, `OUTPUT_LIMIT`, `MEMORY_LIMIT`, `SANDBOX_ERROR`).

3. **Java Spring Boot Gateway (`gateway/`)**:
   - Ingress gateway binding strictly to loopback (`127.0.0.1:8080`) providing a WebSocket endpoint on `/ws` and a REST health check on `/health`.
   - Structured JSON protocol: handles client messages (`room.join`, `room.leave`, `run`) and emits truthful server events (`room.joined`, `room.left`, `run.accepted`, `run.rejected`, `run.completed`, `error`).
   - Bounded in-memory room manager: enforces room capacity (max 100 rooms, max 20 sessions per room), sliding-window rate limiting (30 runs/minute), thread-safe session tracking, disconnect cleanup, and strict room isolation with zero cross-room event leakage.
   - Concurrency governance: enforces an explicit single active run per room policy and a global concurrent execution ceiling (16 runs max).
   - Typed gRPC client: dispatches validated requests to the Kotlin execution worker via generated `pes.execution.v1` stubs, enforces call deadlines, and translates worker responses and transport failures fail-closed.

---

## Roadmap & Phase Status

| Phase | Component | Status | Definition of Done |
|---|---|---|---|
| **Phase 0** | Repo Setup & Governance | Complete (`phase-0-done`) | Pre-commit hook rejects edits to old progress log entries |
| **Phase 1** | C Sandbox v1 (Process Lifecycle & Limits) | Complete (`phase-1-done`) | Correct execution and statuses for clean exit, exit codes, CPU/wall timeouts, memory bomb, output flood; no zombies |
| **Phase 2** | C Sandbox v2 (Linux Isolation & Defences) | Complete (`phase-2-done`) | Fork bomb containment (`pids.max`), filesystem snooping defense, network denial via seccomp; cgroup cleanup verified; 0 stray processes |
| **Phase 3** | Kotlin Worker & Sandboxed Compilation | Complete (`phase-3-done`) | C and Python run end-to-end from a gRPC client (`grpcurl`), with C compilation running inside the sandbox |
| **Phase 4** | Java Gateway & WebSocket Server | Implemented (In Review) | WebSocket client triggers run and receives status events |
| **Phase 5** | Browser Web Client (Monaco Editor) | Planned | Browser editor executes code; renders Idle, Compiling, Running, Timed Out states |
| **Phase 6** | Real-Time Collaboration (Yjs CRDT) | Planned | Concurrent multi-user edits converge deterministically |
| **Phase 7** | Multi-Language Runtimes & Load Benchmarks | Planned | 50 concurrent runs without host instability; documented resource limits |

---

## Honest Limitations

In accordance with project governance rules, the following limitations reflect current functionality:

- **Loopback binding only**: Both the gateway (`127.0.0.1:8080`) and worker (`127.0.0.1:50055`) bind strictly to loopback. Public network ingress, TLS termination, and external identity provider authentication are deferred.
- **In-memory room state**: Collaboration rooms and session memberships are stored entirely in JVM memory and are not persisted across server restarts. Empty rooms are pruned upon disconnection.
- **Event truthfulness (no fake intermediate states)**: Because the worker returns a single final execution response after compilation and execution, the gateway emits truthful lifecycle events (`run.accepted` upon dispatch, followed by `run.completed`). It does not fabricate artificial compiling or running progress states.
- **Unauthenticated sessions**: WebSocket sessions use server-assigned random UUIDs. Client-supplied user identifiers are untrusted and used only for room display metadata.
- **Language support**: The pipeline currently supports C (compiled and executed inside the sandbox) and Python 3 (interpreted inside the sandbox). Runtimes for Rust, Java, and Node.js are planned for Phase 7.
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

# Run the 22-test integration suite (18 Phase 1 + 4 Phase 2 checks)
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

### 4. End-to-End WebSocket Verification
Run the complete stack verification (WebSocket Client -> Java Gateway -> Kotlin Worker -> Sandbox Runner):

```bash
# Verify end-to-end WebSocket execution with real sandboxed C and Python
bash gateway/tests/verify_e2e_websocket.sh 8080 50055
```

### 5. Worker gRPC Direct Verification via `grpcurl`
```bash
# Run live sandbox compilation and execution checks against worker
bash worker/tests/verify_grpcurl.sh 50055 production-runtime

# Run protocol-only verification with the fake runner
bash worker/tests/verify_grpcurl.sh 50055 fake

# Run fail-closed verification (verifies sandbox error when unprivileged)
bash worker/tests/verify_grpcurl.sh 50055 production
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
