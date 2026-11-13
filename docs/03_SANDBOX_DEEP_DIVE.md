# 03 — The C Sandbox, In Depth (the heart of the project)

## Threat model: what can a hostile program do?
| Attack | Example | Defence |
|---|---|---|
| Infinite loop | `while(1);` | `RLIMIT_CPU` + wall-clock timeout (parent kills) |
| Sleep forever | `sleep(9999)` | Wall-clock timeout (CPU limit does NOT catch this) |
| Memory bomb | `malloc` in a loop | `RLIMIT_AS` (simple) → cgroup `memory.max` (accurate) |
| Fork bomb | `while(1) fork();` | `RLIMIT_NPROC`, PID cgroup / PID namespace, kill whole process group |
| Disk fill | write 100 GB | `RLIMIT_FSIZE`, tmpfs with size cap |
| Output flood | print forever | Parent caps bytes read from pipes |
| Read secrets | `open("/etc/passwd")` | chroot/pivot_root + mount namespace, read-only minimal rootfs |
| Network abuse | connect out, scan | `CLONE_NEWNET` (empty network namespace) and/or seccomp |
| Escalation | setuid, ptrace, mount | Drop to unprivileged uid, `PR_SET_NO_NEW_PRIVS`, seccomp-bpf allowlist |
| Zombie leftovers | child survives timeout | `setsid()` + `kill(-pgid, SIGKILL)` |

## Lifecycle of one run (v1)
1. Parent creates two pipes (stdout, stderr) and optionally stdin.
2. `fork()`. Child and parent now run separately.
3. **Child:** `setsid()` (own process group) → redirect fds with `dup2()` → close all other fds → `setrlimit(...)` → scrub environment → `execve(binary, argv, envp)`. If `execve` returns, it failed: write error and `_exit(127)`.
4. **Parent:** close unused pipe ends → `poll()` on stdout+stderr pipes with a deadline → append to capped buffers → on deadline or cap breach, `kill(-pgid, SIGKILL)`.
5. `wait4(pid, &status, 0, &rusage)` → decode with `WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG`; read `ru_maxrss` (peak memory), `ru_utime/ru_stime` (CPU).
6. Print one JSON object with status, exit code, signal, times, memory.

## Why each syscall
- **`fork`**: new process = isolation from the parent's memory.
- **`execve`**: replaces the child with the target program. Use `execve` (explicit env), not `execvp` (searches PATH, inherits env).
- **`setrlimit`**: kernel-enforced ceilings, set in the child *before* exec so the limits apply to the program, not to our runner.
- **`pipe` + `dup2`**: capture stdout/stderr without files.
- **`poll`**: read both pipes without deadlocking (reading stdout to EOF first can block forever if the child fills stderr).
- **`wait4`**: reap the child (no zombies) AND get resource usage in one call.
- **`setsid` / process groups**: one `kill(-pgid)` kills the child and everything it spawned.

## Classic bugs (we will hit these on purpose, then fix)
- Forgetting to close the pipe's write end in the parent → `read` never gets EOF → hang.
- Reading stdout fully, then stderr → deadlock when stderr fills its 64 KB buffer.
- Not reaping → zombies. Not killing the group → orphan fork-bomb survivors.
- `RLIMIT_AS` kills JVM/Node at startup because they reserve huge *virtual* memory. Fix: per-language limits, or use cgroups.
- Race: program exits at the same instant the timeout fires. Decide status by `wait4` result, not by which event you noticed first.
- Leaked fds into the child (open files from the parent). Set `O_CLOEXEC`, close the rest.
- Timer with `alarm()` is coarse and signal-based; prefer `poll()` timeout / `clock_gettime(CLOCK_MONOTONIC)` deadline.

## v2 hardening (Phase 2)
1. `RLIMIT_NPROC`, `RLIMIT_FSIZE`, `RLIMIT_CORE=0`, `RLIMIT_NOFILE`.
2. `prctl(PR_SET_NO_NEW_PRIVS, 1)`.
3. Drop privileges (`setgid`, `setuid` to `nobody`-like uid).
4. Namespaces via `clone`/`unshare`: `CLONE_NEWPID | NEWNS | NEWNET | NEWIPC | NEWUTS` (+ user namespace if running rootless).
5. Minimal rootfs + `pivot_root`, read-only mounts, tmpfs workspace with size cap.
6. seccomp-bpf allowlist (start in log mode, then enforce).
7. cgroups v2: `memory.max`, `pids.max`, `cpu.max`; read `memory.peak` for honest numbers.

## How we will test it (adversarial suite, `sandbox-daemon/tests/evil/`)
`infinite_loop.c`, `sleeper.c`, `memory_bomb.c`, `fork_bomb.c`, `disk_filler.c`, `output_flood.c`, `read_etc_passwd.c`, `net_connect.c`, `ptrace_self.c`, `setuid_try.c`, `escape_child.c` (child that outlives parent). Each must produce a clean, correct status and leave **no stray processes** (`pgrep` check).
