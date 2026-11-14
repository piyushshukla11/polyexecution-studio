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
- **`setsid` / process groups**: one `kill(-pgid, SIGKILL)` terminates the child and processes in its process group. *Caveat:* If a hostile child process forks grandchildren that in turn call `setsid()`, those grandchildren create new sessions and process groups; `kill(-child_pgid)` will not reach them. Phase 2 PID namespaces (`CLONE_NEWPID`) and cgroups v2 (`cgroup.kill`) solve this definitively.

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

## Phase 2 Preflight Capability Audit (`scripts/phase2_preflight.py`)

To verify host kernel capabilities and permission boundaries before writing Phase 2 C code, the Tier C preflight audit tool inspects the environment in read-only mode without mutating system state:

```bash
# Human-readable report
python3 scripts/phase2_preflight.py

# Machine-readable JSON output
python3 scripts/phase2_preflight.py --json
```

### Observed Host Facts vs Inferences

| Dimension | Directly Observed Fact | Status / Interpretation |
|---|---|---|
| **Linux Kernel** | 7.2.8-9-omarchy x86_64 | Standard 64-bit Linux kernel |
| **Namespace Symlinks** | `/proc/self/ns/{cgroup,ipc,mnt,net,pid,time,user,uts}` present | Namespace subsystem symlinks exist in procfs |
| **User Clone Sysctl** | `/proc/sys/kernel/unprivileged_userns_clone = 1` | Unprivileged user cloning enabled in kernel sysctl |
| **Unshare Syscall Probe** | `libc.unshare(CLONE_NEWUSER)` returns `ret=-1, errno=38 (ENOSYS)` | **Observed Fact:** Syscall returns Function not implemented. <br>**Unverified Cause:** Does not prove kernel lacks support; may be intercepted by outer container supervisor, seccomp profile, or LSM. Rootless namespaces unavailable in current container. |
| **Cgroups v2** | Unified cgroup2 mounted at `/sys/fs/cgroup`; delegated user slice at `/sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/` | **Ready:** `cpu`, `memory`, and `pids` controllers are delegated to user `zogratis` (UID 1000). Both `cgroup.procs` and `cgroup.subtree_control` are writable without root. |
| **Seccomp** | Header `/usr/include/seccomp.h` present; `libseccomp.so.2` version 2.6.0 loaded | **Ready:** Seccomp development libraries installed and linkable. |
| **Prctl Flags** | `prctl(PR_GET_NO_NEW_PRIVS)` returns 1; `prctl(PR_GET_PDEATHSIG)` returns 0 | **Ready:** Kernel supports `PR_SET_NO_NEW_PRIVS` and `PR_SET_PDEATHSIG`. |
| **Rootless Setup** | `/etc/subuid` and `/etc/subgid` have `zogratis:100000:65536`; `newuidmap`/`newgidmap` available | **Ready:** Subordinate UID/GID allocations configured. |
| **Privilege Dropping** | `nobody:nobody` account available (UID 65534, GID 65534) | **Ready:** Unprivileged target identity available. |

---

## Phase 2 Adversarial Fixture Audit (`sandbox-daemon/tests/evil/`)

Three Phase 2 adversarial test programs exist in `sandbox-daemon/tests/evil/`. They are compiled under `make evil` but **NEVER executed by the Phase 1 runner**, which lacks filesystem, network, and process isolation.

| Fixture File | Attack Attempted | Containment Failure Criteria | Expected Phase 2 Defense | Distinguishing Policy Enforcement from Unrelated Errors |
|---|---|---|---|---|
| `fork_bomb.c` | Recursive `while(1) fork();` PID exhaustion loop | Host PID table exhausted; `EAGAIN` thrown to host/runner; orphan processes surviving | Cgroups v2 `pids.max` (e.g. 32); `CLONE_NEWPID`; atomic `cgroup.kill` | Check `/sys/fs/cgroup/.../pids.events` for `max > 0` (causal proof that kernel enforced limit). Unrelated errors show `max == 0`. |
| `read_etc_passwd.c` | Attempts `open("/etc/passwd", O_RDONLY)` and reading host secrets | Process opens file, prints `HOST_PASSWD_LEAKED`, and exits 0 | `CLONE_NEWNS` + `pivot_root` minimal rootfs; seccomp open allowlist; unprivileged user | Target exits status 1; open returns `ENOENT` (file absent from private rootfs) or `EPERM`/`EACCES`. Stdout is completely empty. |
| `net_connect.c` | Attempts TCP socket creation and connect to `93.184.216.34:80` | `connect()` succeeds, process prints `NETWORK_CONNECTED`, and exits 0 | `CLONE_NEWNET` (empty network stack); seccomp socket/connect blocking | `connect()` returns `ret < 0, errno = ENETUNREACH` immediately (<1ms) or `socket()` blocked by seccomp (`SIGSYS` or `EPERM`). Does not hang or hit wall timeout. |

---

## Phase 2 Isolation Architecture & Strict Dependency Ordering

To achieve defense in depth without privilege leaks, Phase 2 containerization must follow a strict dependency sequence:

```
[1. User Namespace / Subordinate UID Mapping]
     | (unshare CLONE_NEWUSER or newuidmap/newgidmap)
     v
[2. Unshare Isolation Namespaces]
     | (CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS)
     v
[3. Private Mount Propagation & Minimal Rootfs Setup]
     | (mount --make-rprivate /; bind-mount minimal workspace tmpfs and runtime libs)
     v
[4. Filesystem Swap via pivot_root]
     | (pivot_root to new minimal rootfs; unmount / detach old root)
     v
[5. Drop Auxiliary Groups & Change Identity]
     | (setgroups(0, NULL); setgid(nobody); setuid(nobody))
     v
[6. Enforce No-New-Privileges Boundary]
     | (prctl(PR_SET_NO_NEW_PRIVS, 1) -> mandatory before unprivileged seccomp)
     v
[7. Install Seccomp-BPF Syscall Filter]
     | (Load allowlist: read, write, exit_group, brk, mmap, poll; deny socket, mount, ptrace)
     v
[8. Bind Target Process to Cgroups v2 Resource Hierarchy]
     | (Write PID to cgroup.procs under user slice; enforce pids.max and memory.max)
     v
[9. Replace Process Image via execve]
       (execve(target_binary, argv, envp) with clean environment)
```

### Why Dependency Ordering Matters:
1. **User Namespace before Mount Namespace:** Creating private mounts and invoking `pivot_root` requires `CAP_SYS_ADMIN` within the user namespace.
2. **Mount Setup before Privilege Dropping:** Mounting tmpfs, bind mounting libraries, and swapping roots with `pivot_root` requires namespace privileges; dropping to `nobody` must happen *after* mounts are locked down.
3. **`PR_SET_NO_NEW_PRIVS` before Seccomp:** The Linux kernel strictly forbids unprivileged processes from installing seccomp-bpf filters unless `no_new_privs` has already been set (`prctl(PR_SET_NO_NEW_PRIVS, 1)`).
4. **Seccomp after Filesystem/Socket Setup:** Once the seccomp allowlist is loaded, syscalls like `mount()`, `pivot_root()`, and `socket()` are permanently blocked for that process and its children.

---

## Systems Correctness Notes

### 1. `PR_SET_PDEATHSIG` Limitation with Grandchildren
`prctl(PR_SET_PDEATHSIG, SIGKILL)` asks the Linux kernel to send `SIGKILL` to the calling process when its immediate parent dies.
* **The Failure Mode:** Signals delivered to a direct child do **not** propagate down process trees automatically. If the direct child spawns grandchild processes or detaches background daemons, those grandchildren do not receive the signal. When the direct child dies, grandchildren are reparented to PID 1 (`init` / `systemd`) as orphaned background workloads.
* **The Solution:** True descendant lifecycle containment requires **PID namespaces (`CLONE_NEWPID`)** (where terminating PID 1 terminates all processes in the namespace) and **cgroups v2 (`cgroup.kill`)** (which sends `SIGKILL` to every task in the cgroup simultaneously).

### 2. Disambiguating unshare ENOSYS
In containerized development environments, `libc.unshare(CLONE_NEWUSER)` may return `errno=38 (ENOSYS)`.
* **Observed Fact:** The kernel returns `ENOSYS` ("Function not implemented").
* **Separation from Inferences:** Do not assert without proof that an outer container seccomp filter caused the error. `ENOSYS` can occur because the kernel was built without `CONFIG_USER_NS`, because an outer container filter rewrites the syscall, or because a security module (LSM) blocks it. Document the observed errno and record the cause as unverified.

### 3. Resource Limits vs Sandbox Isolation (Defense in Depth)
A common beginner misconception is assuming that resource limits (`RLIMIT_CPU`, `RLIMIT_AS`) constitute sandboxing.
* **Resource Limits (`setrlimit`, cgroups v2):** Restrict the *quantity* of resources consumed (CPU seconds, memory bytes, process count, file size).
* **Isolation Controls (Namespaces, seccomp, pivot_root, privilege dropping):** Restrict *access* to system resources (host files, network sockets, other processes, kernel syscalls).
* A process constrained by `RLIMIT_CPU` and `RLIMIT_AS` can still read `/etc/passwd`, exfiltrate private SSH keys over the network, or scan local network subnets unless namespaces and seccomp are actively enforced.

## How we will test it (adversarial suite, `sandbox-daemon/tests/evil/`)
`infinite_loop.c`, `sleeper.c`, `memory_bomb.c`, `fork_bomb.c`, `disk_filler.c`, `output_flood.c`, `read_etc_passwd.c`, `net_connect.c`, `ptrace_self.c`, `setuid_try.c`, `escape_child.c` (child that outlives parent). Each must produce a clean, correct status and leave **no stray processes** (`pgrep` check).
