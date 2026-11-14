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
| **Unshare Syscall Probe** | `libc.unshare(CLONE_NEWUSER)` returns `ret=-1, errno=38 (ENOSYS)` | **Observed Fact:** Syscall returns Function not implemented. <br>**Unverified Cause:** Does not prove kernel lacks support; cause is UNKNOWN (leaves open whether kernel lacks support, an outer container supervisor intercepts the syscall, or an LSM blocks it). |
| **Cgroups v2** | Unified cgroup2 mounted at `/sys/fs/cgroup`; delegated user slice at `/sys/fs/cgroup/user.slice/user-1000.slice/user@1000.service/` | **Controllers PRESENT; Enforcement INFERRED:** Controllers `cpu`, `memory`, and `pids` are present and user slice directory is writable. However, creating a child cgroup and writing/enforcing `pids.max` or `memory.max` were NOT verified (read-only audit). |
| **Seccomp** | Header `/usr/include/seccomp.h` present; `libseccomp.so.2` version 2.6.0 loaded; `PR_GET_NO_NEW_PRIVS` = 1 | **Toolchain PRESENT; Enforcement INFERRED:** Seccomp development files are present, but compiling and installing an active BPF filter in the kernel was NOT verified (read-only audit). |
| **Rootless Setup** | `/etc/subuid` and `/etc/subgid` have `zogratis:100000:65536`; `newuidmap`/`newgidmap` available | **Files PRESENT; Mapping UNAVAILABLE:** Subordinate ID files and mapping binaries are present, but cannot establish an unprivileged sandbox while `unshare(CLONE_NEWUSER)` fails with ENOSYS. |
| **Privilege Dropping** | `nobody:nobody` account available (UID 65534, GID 65534) | **Identity PRESENT; Transition INFERRED:** Unprivileged identity exists, but executing `setuid`/`setgid` was NOT tested (read-only audit). |

---

## Phase 2 Adversarial Fixture Audit (`sandbox-daemon/tests/evil/`)

Three Phase 2 adversarial test programs exist in `sandbox-daemon/tests/evil/`. They are compiled under `make evil` but **NEVER executed by the Phase 1 runner**, which lacks filesystem, network, and process isolation.

| Fixture File | Attack Attempted | Containment Failure Criteria | Expected Phase 2 Defense | How Test Distinguishes Policy Enforcement from Unrelated Errors |
|---|---|---|---|---|
| `fork_bomb.c` | Recursive `while(1) fork();` PID exhaustion loop | Host PID table exhausted; `EAGAIN` thrown to host/runner; orphan processes surviving | Cgroups v2 `pids.max` (e.g. 16 or 32); `CLONE_NEWPID`; atomic `cgroup.kill` | Inspect `/sys/fs/cgroup/.../pids.events` for `max > 0` (causal proof that kernel enforced task ceiling). Unrelated failures show `max == 0`. Labeled DANGEROUS: DO NOT EXECUTE. |
| `read_etc_passwd.c` | Attempts host filesystem snooping | Unique host canary file read and token matched, or host user (`zogratis:`) leaked from `/etc/passwd`. Exit 0. | `CLONE_NEWNS` + `pivot_root` minimal rootfs; seccomp open allowlist; unprivileged user | Emits structured `DIAG:` to stderr. Accepts unique canary path via `argv[1]` / `PES_CANARY_PATH` and verification token via `argv[2]` / `PES_CANARY_TOKEN`. Distinguishes host data leak (exit 0) from clean container-local passwd files or policy denial (`ENOENT`, `EACCES`, `EPERM` -> exit 1). Unrelated errors exit 2. Canary is created and removed only by future test harness. |
| `net_connect.c` | Attempts TCP socket creation and connect | Outbound TCP connection succeeded. Exit 0. | `CLONE_NEWNET` (empty network stack); seccomp socket/connect blocking | Emits structured `DIAG:` to stderr. Accepts configurable target IP/port via `argv[1]` / `argv[2]` or env vars. Distinguishes proven network namespace policy (`ENETUNREACH`) and seccomp policy (`EPERM`/`EACCES` -> exit 1) from ambient network drops (`ETIMEDOUT` -> exit 2) or reachability leaks (`ECONNREFUSED` -> exit 2). |

---

### Network Isolation Contracts & The Limits of Interpreting `ENETUNREACH`
When designing network isolation tests in Linux:
1. **The Meaning of `ENETUNREACH`:** In Linux, `connect()` returns `ret < 0, errno = ENETUNREACH` (101, "Network is unreachable") when there is no matching route in the kernel routing table. Inside a newly unshared `CLONE_NEWNET` namespace with no interfaces configured, `ENETUNREACH` is returned synchronously by the kernel in microseconds.
2. **The Environment Caveat:** If the host machine itself is offline (e.g. airplane mode or disconnected interface), an *unisolated* host process would also get `ENETUNREACH`! Therefore, a test suite must not rely on public internet addresses (`93.184.216.34:80`). Instead, a future test harness must run a local host listener outside the sandbox and verify reachability from the host *before* asserting that the sandbox gets `ENETUNREACH`.
3. **Why `ECONNREFUSED` is a Breach of Isolation:** If `connect()` returns `ECONNREFUSED` (111, "Connection refused"), the TCP SYN packet successfully traversed the network stack and reached a host that sent back a TCP RST packet! This proves the destination was **reachable** and network isolation failed! The test harness classifies `ECONNREFUSED` as an unexpected failure / reachability leak, never as isolation proof.
4. **Why `ETIMEDOUT` is Not Proof of Isolation:** If `connect()` hangs and times out with `ETIMEDOUT` (110), it means packets were dropped by an intermediate network firewall, NAT, or ISP filter. It does NOT prove Linux kernel namespace isolation.

---

## Phase 2 Isolation Architecture & Strict Dependency Ordering

To achieve defense in depth without privilege leaks or self-blocking race conditions, Phase 2 containerization must strictly separate **Parent Supervisor Responsibilities** from the **Isolated Child Lifecycle**. 

A common pitfall is attempting to write `uid_map`/`gid_map` before the user namespace exists, or expecting `unshare(CLONE_NEWPID)` to place the calling process into the new PID namespace. In Linux (`user_namespaces(7)`, `pid_namespaces(7)`, `unshare(2)`):
* `/proc/[pid]/uid_map` belongs to the user namespace that `pid` currently inhabits; the parent cannot map subordinate IDs until **after** the child has invoked `unshare(CLONE_NEWUSER)`.
* `unshare(CLONE_NEWPID)` does **not** move the caller into the new namespace; instead, the **first child created afterward via fork()** becomes PID 1 of the new PID namespace.

### The Concrete Handshake Sequence:

```
+-----------------------------------------------------------------------------------+
|                           PARENT SUPERVISOR LIFECYCLE                             |
+-----------------------------------------------------------------------------------+
| Step 1: Create Job Cgroup Directory                                               |
|         -> mkdir /sys/fs/cgroup/user.slice/.../pes-job-<id>/                      |
|         -> Write resource ceilings: pids.max = 32, memory.max = 128M              |
|         -> Executed by parent holding caller's delegated permissions              |
+-----------------------------------------------------------------------------------+
| Step 2: Create Inter-Process Sync Pipes (O_CLOEXEC)                               |
|         -> child_to_parent_sync: child signals when user namespace is created     |
|         -> parent_to_child_sync: parent signals when cgroup & ID mapping are done |
+-----------------------------------------------------------------------------------+
| Step 3: Fork Sandbox Helper (Child 1)                                             |
|         -> Parent forks Child 1; Child 1 begins namespace setup                   |
+-----------------------------------------------------------------------------------+

                                      | (Child 1 runs)
                                      v

+-----------------------------------------------------------------------------------+
|                        SANDBOX HELPER LIFECYCLE (Child 1)                         |
+-----------------------------------------------------------------------------------+
| Step 4: Create User Namespace                                                     |
|         -> unshare(CLONE_NEWUSER)                                                 |
|         -> Creates new user namespace; /proc/self/uid_map now belongs to it       |
| Step 5: Pause at Synchronization Point                                            |
|         -> Write 1 byte to child_to_parent_sync                                   |
|         -> Block reading 1 byte from parent_to_child_sync                         |
+-----------------------------------------------------------------------------------+

                                      | (Parent unblocks)
                                      v

+-----------------------------------------------------------------------------------+
|                           PARENT SUPERVISOR RESUMES                               |
+-----------------------------------------------------------------------------------+
| Step 6: Attach Child 1 to Cgroup                                                  |
|         -> Parent writes Child 1 PID into pes-job-<id>/cgroup.procs               |
|         -> Must happen while parent has user UID 1000 permissions                 |
|         -> Guarantees Child 1 (and all future descendants) are bound to cgroup    |
+-----------------------------------------------------------------------------------+
| Step 7: Establish Subordinate UID and GID Mappings                                |
|         -> Parent executes newuidmap / newgidmap targeting Child 1 PID            |
|         -> Maps host UID 1000 to container UID 0, and subuid range (65536 IDs)    |
|         -> Valid because Child 1 created the user namespace in Step 4            |
+-----------------------------------------------------------------------------------+
| Step 8: Release Child 1                                                           |
|         -> Parent writes 1 byte to parent_to_child_sync                           |
+-----------------------------------------------------------------------------------+

                                      | (Child 1 unblocks)
                                      v

+-----------------------------------------------------------------------------------+
|                        SANDBOX HELPER CONTINUES (Child 1)                         |
+-----------------------------------------------------------------------------------+
| Step 9: Unshare Remaining Isolation Namespaces                                    |
|         -> unshare(CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC)     |
|         -> KEY SEMANTICS: Child 1 remains in parent PID namespace; subsequent     |
|            children created by Child 1 will inhabit the new PID namespace         |
+-----------------------------------------------------------------------------------+
| Step 10: Private Mount Setup & pivot_root                                         |
|          -> mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL)                     |
|          -> Mount private tmpfs workspace and bind-mount read-only libraries      |
|          -> pivot_root(new_root, put_old); detach old root                        |
+-----------------------------------------------------------------------------------+
| Step 11: Drop Auxiliary Groups & Drop Privileges                                  |
|          -> setgroups(0, NULL); setgid(nobody_gid); setuid(nobody_uid)            |
|          -> Uses mapped IDs established by parent in Step 7                       |
+-----------------------------------------------------------------------------------+
| Step 12: Fork Target Process (Child 2)                                            |
|          -> Child 1 invokes fork() to spawn Child 2                               |
|          -> KEY SEMANTICS: Child 2 is born as PID 1 of the new PID namespace!    |
+-----------------------------------------------------------------------------------+

                                      | (Child 2 runs as PID 1)
                                      v

+-----------------------------------------------------------------------------------+
|                           TARGET PROCESS (Child 2, PID 1)                         |
+-----------------------------------------------------------------------------------+
| Step 13: Enforce No-New-Privileges Boundary                                       |
|          -> prctl(PR_SET_NO_NEW_PRIVS, 1) [mandatory before unprivileged seccomp] |
+-----------------------------------------------------------------------------------+
| Step 14: Install Seccomp-BPF Syscall Filter                                       |
|          -> Install allowlist validated against runtime dynamic linker (ld.so)    |
|          -> Permanent lockdown: blocks socket, mount, pivot_root, ptrace, setuid  |
+-----------------------------------------------------------------------------------+
| Step 15: Execute Target Program via execve                                        |
|          -> execve(binary, argv, envp) with scrubbed environment                 |
+-----------------------------------------------------------------------------------+
| Step 16: Supervise & Reap (Child 1 & Parent)                                      |
|          -> Child 1 waits on Child 2 (PID 1 of namespace); reaps all exit statuses|
|          -> If Child 2 dies, kernel terminates all processes in the namespace     |
|          -> Parent supervisor enforces wall timeouts and cleans up cgroups        |
+-----------------------------------------------------------------------------------+
```

### Architectural Notes & Assumptions:
1. **User Namespace Ordering:** `/proc/[pid]/uid_map` exists only after the child creates the user namespace (`unshare(CLONE_NEWUSER)`). The synchronization pipes ensure the child pauses until the parent has written the mappings using `newuidmap`/`newgidmap`.
2. **PID Namespace Two-Process Pattern:** Because `unshare(CLONE_NEWPID)` does not place the calling process into the new namespace, an intermediate fork is required. Child 1 unshares `CLONE_NEWPID` and forks Child 2. Child 2 becomes PID 1 of the new namespace.
3. **Environment Limitation:** In our current container execution environment, `libc.unshare(CLONE_NEWUSER)` returns `errno=38 (ENOSYS)`. This full handshake cannot be executed live in this container until unprivileged user namespaces are available.

### Why This Ordering is Essential (Avoiding Classic Systems Traps):

1. **Why Cgroup Placement Must Happen in the Parent (Step 1 & 3):**
   * *Permission Trap:* Moving a process into a cgroup requires write permission on `cgroup.procs`. In user delegation, the directory is owned by the host user (UID 1000). If the child process dropped identity to `nobody` (UID 65534) before writing to `cgroup.procs`, the write fails with `EACCES`.
   * *Seccomp Self-Block:* Once a seccomp filter is loaded (Step 10), writing to `/sys/fs/cgroup` is permanently blocked. The parent must bind the child to the cgroup *before* the child drops privileges or locks itself down.

2. **Why User Namespace Mapping Must Precede Identity Dropping (Step 3 before Step 8):**
   * Inside a newly created user namespace (`CLONE_NEWUSER`), UIDs and GIDs start in an unmapped state.
   * Calling `setuid(nobody_uid)` or `setgid(nobody_gid)` fails immediately (`EINVAL` or `EPERM`) if those IDs have not been registered in `/proc/[pid]/uid_map` and `/proc/[pid]/gid_map`. The parent must establish the ID mapping before the child attempts to switch to `nobody`.

3. **Why Seccomp Allowlist Requires Dynamic Linker Mapping (Step 10):**
   * A common beginner mistake is assuming a simple 6-syscall allowlist (`read`, `write`, `exit_group`, `brk`, `mmap`, `poll`) can run real programs.
   * In Linux, dynamic binaries require the dynamic linker (`ld.so`) and C runtime to execute dozens of low-level syscalls during process initialization: `arch_prctl`, `set_tid_address`, `set_robust_list`, `prlimit64`, `rseq`, `futex`, `mprotect`, `rt_sigaction`, `rt_sigprocmask`, and `getrandom`.
   * If any of these are omitted, the process is instantly terminated by `SIGSYS` before reaching `main()`.
   * *Required Approach:* Develop seccomp filters in two stages:
     1. **Audit / Log Mode (`SECCOMP_RET_LOG`):** Intercept and record all syscalls made during legitimate runs of target language runtimes.
     2. **Strict Enforcement Mode (`SECCOMP_RET_KILL_PROCESS`):** Lock down the filter only after integration tests prove the allowlist permits all runtime prerequisites.

4. **`PR_SET_PDEATHSIG` Limitation with Process Trees:**
   * `prctl(PR_SET_PDEATHSIG, SIGKILL)` delivers a signal to the **immediate child only** when the calling parent process terminates.
   * Signals delivered to a child do **not** propagate down process trees automatically. If the child process forks grandchildren or starts background processes, those grandchildren do not receive `PDEATHSIG`. When the child terminates, grandchildren are reparented to PID 1 (`init` / `systemd`) as orphaned background workloads.
   * True descendant lifecycle containment requires **PID namespaces (`CLONE_NEWPID`)** (where terminating PID 1 terminates all processes in the namespace) and **cgroups v2 (`cgroup.kill`)** (which sends `SIGKILL` to every process in the cgroup simultaneously).

5. **Disambiguating unshare ENOSYS:**
   * *Observed Evidence:* In our environment, `libc.unshare(CLONE_NEWUSER)` returned `ret=-1, errno=38 (ENOSYS)`.
   * *Separation from Inferences:* `ENOSYS` ("Function not implemented") is recorded as observed fact. Its underlying cause remains an unverified hypothesis: it leaves open whether the running kernel lacks user namespace support (`CONFIG_USER_NS`), an outer container supervisor seccomp profile intercepts the call, an LSM policy blocks it, or container hypervisor restrictions apply. Do not assume or assert container seccomp filtering without independent proof.

### 3. Resource Limits vs Sandbox Isolation (Defense in Depth)
A common beginner misconception is assuming that resource limits (`RLIMIT_CPU`, `RLIMIT_AS`) constitute sandboxing.
* **Resource Limits (`setrlimit`, cgroups v2):** Restrict the *quantity* of resources consumed (CPU seconds, memory bytes, process count, file size).
* **Isolation Controls (Namespaces, seccomp, pivot_root, privilege dropping):** Restrict *access* to system resources (host files, network sockets, other processes, kernel syscalls).
* A process constrained by `RLIMIT_CPU` and `RLIMIT_AS` can still read `/etc/passwd`, exfiltrate private SSH keys over the network, or scan local network subnets unless namespaces and seccomp are actively enforced.

## How we will test it (adversarial suite, `sandbox-daemon/tests/evil/`)
`infinite_loop.c`, `sleeper.c`, `memory_bomb.c`, `fork_bomb.c`, `disk_filler.c`, `output_flood.c`, `read_etc_passwd.c`, `net_connect.c`, `ptrace_self.c`, `setuid_try.c`, `escape_child.c` (child that outlives parent). Each must produce a clean, correct status and leave **no stray processes** (`pgrep` check).
