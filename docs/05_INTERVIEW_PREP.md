# 05 — Interview Prep for PES

## 30-second pitch
"I am building PolyExecution Studio, a collaborative code-execution platform. The planned system lets users edit together in a browser and send runs through backend services to a C sandbox. The current implementation work is the C process runner and its Phase 1 regression suite; the gateway, worker, browser, collaboration layer, and stronger isolation controls are planned work."

## Honest AI statement (use this)
"I used an AI coding agent to implement parts of the C runner, tests, and project scaffolding. I am studying and reviewing that work, completing owner-authored exercises, and keeping the implementation's current limits separate from planned features. The progress log records why changes were made and what evidence was collected."

## Questions you must be able to answer (answers sketched; make them yours)
**Sandbox**
1. *Does `setrlimit` sandbox a process?* No. It caps resources only; no filesystem, network or syscall isolation.
2. *Why set limits in the child before `execve`?* Limits are inherited across exec; setting them in the parent would constrain the runner itself.
3. *`RLIMIT_CPU` vs wall timeout?* CPU counts running time only; a sleeping program never hits it. Need both.
4. *Why `poll` instead of sequential reads?* Avoids deadlock when one pipe fills while we block on the other.
5. *How do you kill a fork bomb?* `setsid` so it has its own process group, `kill(-pgid, SIGKILL)`; plus `RLIMIT_NPROC`/pids cgroup to stop the explosion.
6. *Why `wait4` over `waitpid`?* Same reaping, plus `rusage` for peak RSS and CPU time.
7. *Why does `RLIMIT_AS` break the JVM?* It limits virtual address space; the JVM reserves far more than it uses.
8. *What are zombies and how did you avoid them?* Exited children not yet waited on; always `wait4`.
9. *Namespaces vs cgroups?* Namespaces limit what a process can *see* (pids, mounts, net); cgroups limit what it can *use* (memory, cpu, pids).
10. *What does seccomp-bpf do? Allowlist or denylist?* Filters syscalls; allowlist is safer. `NO_NEW_PRIVS` is required to install it unprivileged.
11. *What does `execve` returning mean?* It failed; the child must `_exit`, never continue as a copy of the runner.
12. *How do you get accurate peak memory?* `ru_maxrss` from `wait4`, or cgroup `memory.peak`.
13. *Race between exit and timeout?* Decide status from the `wait4` result.
14. *Is this safe for production?* Not by itself. Real systems add VM/microVM isolation (gVisor, Firecracker), and keep the kernel patched. Say this out loud; it shows maturity.

**Distributed / backend**
15. *Why gRPC?* Typed contract, HTTP/2, streaming, codegen. Cost: tooling complexity.
16. *Why coroutines?* Cheap concurrency for I/O-bound jobs.
17. *What if a worker dies mid-job?* Job timeout at gateway, status SANDBOX_ERROR/WORKER_LOST, idempotent job IDs, no automatic retry of user code without care.
18. *How would you scale it?* Stateless workers behind a queue, per-worker concurrency limits, gateway rate-limits, sticky rooms for WebSocket.
19. *Backpressure?* Bounded job queue, reject with "busy" instead of unbounded memory growth.

**Collaboration**
20. *What is a CRDT? Why not locks?* Data structure whose concurrent edits merge deterministically without coordination; locks hurt latency and offline edits.
21. *CRDT vs OT?* OT needs a central transform server and careful ordering; CRDT merges peer-to-peer. Yjs is a CRDT.
22. *What does the gateway store for a room?* The Yjs document state/updates, not parsed text logic.

**Process questions**
23. *What was the hardest bug?* (Have a real one from PROGRESS.md.)
24. *What would you do differently?* (e.g. merge gateway and worker, use Firecracker.)
25. *How did you test security?* The evil-program suite, plus stray-process checks.

## Self-test ritual
After each phase, answer the matching questions out loud with no notes. Log the result in PROGRESS.md as a `LEARNING` entry.

## Required learning sign-off before calling PES complete

This is the project-wide final learning checklist. Piyush asked to finish technical work across the planned phases first and complete these questions and exercises at the end. Keep every item open until Piyush can answer or perform it independently and the tutor has reviewed the result. Agent-authored answers or code do not count as Piyush's completion. Open items do not block authorized technical phase work, but technical DoD, security review, and Git checks still apply. The whole project remains in progress until this checklist is complete.

### Required explain-back questions

- [ ] **Architecture:** Explain the complete planned flow from Monaco/Yjs in the browser, through WebSocket to the Java Gateway, gRPC/protobuf to the Kotlin Worker, and process launch through the C runner. Explain which parts are implemented and which remain planned.
- [ ] **Yjs and CRDT:** Explain what a CRDT is, what Yjs provides, and how concurrent edits can converge.
- [ ] **gRPC:** Explain what gRPC does in the planned backend, what protobuf defines, and how this differs from WebSocket and HTTP.
- [ ] **Process lifecycle:** Explain what parent and child each do after `fork()`, what successful `execve()` does, and why the parent must reap the child with `wait4()`.
- [ ] **Wait status:** Explain when to use `WIFEXITED`, `WEXITSTATUS`, `WIFSIGNALED`, and `WTERMSIG`, and what a zombie is.
- [ ] **Setup-error protocol:** Explain how the CLOEXEC error pipe distinguishes a failed `execve()` from a valid target that exits 127, and why the child uses `_exit()` after setup failure.
- [ ] **Pipes and polling:** Explain why stdout and stderr use separate pipes, how `poll()` avoids sequential-read deadlock, and why unused pipe ends must be closed.
- [ ] **Process groups:** Explain `setsid()` and group signaling, what descendants it helps clean up, and why a descendant that creates a new session can escape that mechanism.
- [ ] **Timeouts:** Explain CPU time versus wall-clock time, why sleeping uses little CPU, and how `SIGXCPU` differs from a wall-clock kill.
- [ ] **Memory reporting:** Explain the difference between `RLIMIT_AS` and resident memory (`ru_maxrss`), why the RSS threshold is only a heuristic, and why it cannot prove the cause of a segmentation fault.
- [ ] **Result contract:** Explain the difference between target exit code and API status, including exit 0, nonzero exit, valid exit 127, and executable/setup failure.
- [ ] **Output and JSON:** Explain output caps and how byte-oriented Latin-1 escaping makes arbitrary process output reconstructable after JSON parsing.
- [ ] **Isolation limits:** Explain what `setrlimit()` does and does not isolate, and distinguish namespaces, cgroups, seccomp, and privilege dropping. Clearly identify which are still planned.
- [ ] **Capability order:** Explain why procfs mount, bounding-set pruning, supplementary-group clearing, identity drop, `PR_SET_NO_NEW_PRIVS`, capability verification, seccomp, and `execve()` have an order. State that current checks of this contract are documentation tests, not runtime enforcement.
- [ ] **Evidence:** Explain how the adversarial test suite supports a claim, what its cleanup check can prove, and the difference between an observed result, an inference, and an unknown.
- [ ] **Trade-offs and ownership:** Explain why C, gRPC, Kotlin coroutines, a separate Java Gateway, and Yjs were chosen; state one alternative and one cost. Describe AI assistance accurately and identify your own contributions.
- [ ] **Project story:** Describe one real bug or review correction, how it was found, how it was fixed, and what test evidence supports the fix.

The existing 25 interview questions above are also required: answer every one without relying on the sketched answer. These added questions cover topics that came up during tutoring and agent review.

### Required owner-authored regression exercises

The agent has already added test cases for these behaviors. That does not complete the exercises for Piyush. For each item, first predict the result, then independently improve or reconstruct the assertion in `sandbox-daemon/tests/test_runner.py`, run `make test`, and explain what the assertion proves. The agent may give hints and review your diff but must not write the solution for you.

- [ ] **Empty output streams:** Use the `exit0` fixture. Verify `status`, `exit_code`, `signal`, `stdout`, and `stderr`, including that both streams are exactly empty.
- [ ] **CPU versus wall timeout:** Use the `loop` fixture with a generous wall timeout. Verify `TIMEOUT`, `SIGXCPU`, CPU time near its configured limit, and wall time below the wall deadline.
- [ ] **Stderr output cap:** Use `stderr_only` with a 1 KiB configured cap. Verify `OUTPUT_LIMIT`, the exact stderr cap, and empty stdout.

### Required owner-run experiments and core-code recall

- [ ] Independently run and explain normal exit, nonzero exit, missing executable, ordinary SIGSEGV, CPU loop, sleeper, output flood, and JSON byte round-trip checks.
- [ ] Perform the documented external `kill -9` experiment and explain the signal reported by the runner.
- [ ] Close the source and reconstruct the `run_sandbox()` control flow from memory; compare it with the implementation and explain any gaps.
- [ ] When Phase 2 is implemented, complete its own explicitly assigned runtime experiments before claiming its isolation DoD. Documentation-contract tests alone do not count as runtime evidence.

### Completion rule

PES may be called complete only after every required checkbox in this section and every question in the existing interview-prep list has been answered or performed by Piyush, reviewed by the tutor, and recorded in `PROGRESS.md` with a `LEARNING` entry. Remaining required exercises do not stop authorized project work, but they do keep the overall project status at **in progress**.
