# 05 — Interview Prep for PES

## 30-second pitch
"I built a collaborative code execution platform. Users edit together in the browser, and runs go through a gRPC pipeline to a Kotlin worker that invokes a C sandbox I wrote using fork, execve, setrlimit, pipes and wait4, hardened with namespaces, seccomp and cgroups. I tested it with an adversarial suite: fork bombs, memory bombs, file-read and network attempts."

## Honest AI statement (use this)
"I used an AI agent for boilerplate and to review my work. I designed the architecture, wrote/traced the sandbox core, and I can explain every syscall. I kept a progress log and rules file so each change had a recorded reason."

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
