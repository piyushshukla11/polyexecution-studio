#define _GNU_SOURCE

#include "phase2_privilege.h"

#include <errno.h>
#include <stdbool.h>
#include <grp.h>
#include <linux/capability.h>
#include <linux/sched.h>
#include <sched.h>
#include <seccomp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

int phase2_child_create_namespaces(void) {
    int flags = CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWNS | CLONE_NEWIPC | CLONE_NEWUTS;
    return unshare(flags);
}

int phase2_set_parent_death_signal(pid_t expected_parent) {
    if (prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0) < 0) return -1;
    if (getppid() != expected_parent) {
        errno = ESRCH;
        return -1;
    }
    return 0;
}

int phase2_privilege_read_cap_last_cap(int *last_cap) {
    FILE *file = fopen("/proc/sys/kernel/cap_last_cap", "re");
    if (!file) return -1;
    int value = -1;
    int result = fscanf(file, "%d", &value);
    int saved_errno = errno;
    if (fclose(file) < 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result != 1 || value < 0 || value > 4096) {
        errno = saved_errno ? saved_errno : EPROTO;
        return -1;
    }
    *last_cap = value;
    return 0;
}

static int prune_capability_bounding_set(int last_cap) {
    for (int cap = 0; cap <= last_cap; cap++) {
        if (cap == CAP_SETPCAP) continue;
        if (prctl(PR_CAPBSET_DROP, cap, 0, 0, 0) < 0) return -1;
    }
    if (prctl(PR_CAPBSET_DROP, CAP_SETPCAP, 0, 0, 0) < 0) return -1;
    return 0;
}

static int clear_ambient_capabilities(void) {
    return prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0);
}

static int clear_supplementary_groups(void) {
    return setgroups(0, NULL);
}

static int set_unprivileged_identity(void) {
    if (setresgid(65534, 65534, 65534) < 0) return -1;
    if (setresuid(65534, 65534, 65534) < 0) return -1;
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0
    };
    struct __user_cap_data_struct data[2] = {{0}};
    if (syscall(SYS_capset, &header, data) < 0) return -1;
    return 0;
}

static int verify_zero_capability_masks(void) {
    FILE *file = fopen("/proc/self/status", "re");
    if (!file) return -1;
    const char *names[] = {"CapInh:", "CapPrm:", "CapEff:", "CapBnd:", "CapAmb:"};
    bool seen[5] = {false, false, false, false, false};
    char *line = NULL;
    size_t capacity = 0;
    int result = 0;
    while (getline(&line, &capacity, file) >= 0) {
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            size_t len = strlen(names[i]);
            if (strncmp(line, names[i], len) != 0) continue;
            char *value = line + len;
            while (*value == ' ' || *value == '\t') value++;
            char *digits = value;
            while ((*digits >= '0' && *digits <= '9') ||
                   (*digits >= 'a' && *digits <= 'f') ||
                   (*digits >= 'A' && *digits <= 'F')) digits++;
            if (digits - value != 16) result = -1;
            errno = 0;
            char *end = NULL;
            unsigned long long mask = strtoull(value, &end, 16);
            if (errno != 0 || end != digits || mask != 0) result = -1;
            while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') end++;
            if (*end != '\0') result = -1;
            seen[i] = true;
        }
    }
    for (size_t i = 0; i < sizeof(seen) / sizeof(seen[0]); i++) {
        if (!seen[i]) result = -1;
    }
    int saved_errno = errno;
    free(line);
    if (fclose(file) < 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result < 0) errno = saved_errno ? saved_errno : EPERM;
    return result;
}

static int verify_zero_capability_state(int last_cap) {
    struct __user_cap_header_struct header = {
        .version = _LINUX_CAPABILITY_VERSION_3,
        .pid = 0
    };
    struct __user_cap_data_struct data[2] = {{0}};
    if (syscall(SYS_capget, &header, data) < 0) return -1;
    for (size_t i = 0; i < 2; i++) {
        if (data[i].inheritable != 0 || data[i].permitted != 0 || data[i].effective != 0) {
            errno = EPERM;
            return -1;
        }
    }
    for (int cap = 0; cap <= last_cap; cap++) {
        int bounding = prctl(PR_CAPBSET_READ, cap, 0, 0, 0);
        if (bounding < 0) return -1;
        if (bounding != 0) {
            errno = EPERM;
            return -1;
        }
        int ambient = prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_IS_SET, cap, 0, 0);
        if (ambient < 0) return -1;
        if (ambient != 0) {
            errno = EPERM;
            return -1;
        }
    }
    return 0;
}

static int allow_syscall(scmp_filter_ctx filter, int syscall_number) {
    if (syscall_number == __NR_clone) {
        unsigned int forbidden = CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWCGROUP |
                                 CLONE_NEWUTS | CLONE_NEWIPC | CLONE_NEWNET | CLONE_NEWPID;
        int rc = seccomp_rule_add(filter, SCMP_ACT_ALLOW, syscall_number, 1,
                                  SCMP_A0(SCMP_CMP_MASKED_EQ, forbidden, 0));
        if (rc < 0) {
            errno = -rc;
            return -1;
        }
        return 0;
    }
    int rc = seccomp_rule_add(filter, SCMP_ACT_ALLOW, syscall_number, 0);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return 0;
}

static int deny_socket_syscall(scmp_filter_ctx filter, int syscall_number) {
    int rc = seccomp_rule_add(filter, SCMP_ACT_ERRNO(EPERM), syscall_number, 0);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return 0;
}

static int deny_syscall_enosys(scmp_filter_ctx filter, int syscall_number) {
    int rc = seccomp_rule_add(filter, SCMP_ACT_ERRNO(ENOSYS), syscall_number, 0);
    if (rc < 0) {
        errno = -rc;
        return -1;
    }
    return 0;
}

static int install_seccomp_allowlist(void) {
    scmp_filter_ctx filter = seccomp_init(SCMP_ACT_KILL_PROCESS);
    if (!filter) {
        errno = ENOMEM;
        return -1;
    }
    const int syscalls[] = {
        SCMP_SYS(read), SCMP_SYS(write), SCMP_SYS(readv), SCMP_SYS(writev), SCMP_SYS(close),
        SCMP_SYS(fstat), SCMP_SYS(newfstatat), SCMP_SYS(stat), SCMP_SYS(lstat), SCMP_SYS(lseek),
        SCMP_SYS(mmap), SCMP_SYS(mprotect), SCMP_SYS(munmap), SCMP_SYS(mremap), SCMP_SYS(brk),
        SCMP_SYS(madvise),
        SCMP_SYS(rt_sigaction), SCMP_SYS(rt_sigprocmask), SCMP_SYS(rt_sigreturn), SCMP_SYS(sigaltstack),
        SCMP_SYS(ioctl), SCMP_SYS(access), SCMP_SYS(faccessat), SCMP_SYS(faccessat2),
        SCMP_SYS(open), SCMP_SYS(openat), SCMP_SYS(readlink), SCMP_SYS(readlinkat), SCMP_SYS(pread64),
        SCMP_SYS(getpid), SCMP_SYS(getppid), SCMP_SYS(gettid), SCMP_SYS(getuid), SCMP_SYS(geteuid),
        SCMP_SYS(getgid), SCMP_SYS(getegid), SCMP_SYS(arch_prctl), SCMP_SYS(set_tid_address),
        SCMP_SYS(set_robust_list), SCMP_SYS(prlimit64), SCMP_SYS(rseq), SCMP_SYS(futex),
        SCMP_SYS(clock_gettime), SCMP_SYS(clock_nanosleep), SCMP_SYS(nanosleep), SCMP_SYS(getrandom),
        SCMP_SYS(getrusage), SCMP_SYS(getrlimit), SCMP_SYS(times), SCMP_SYS(gettimeofday),
        SCMP_SYS(statfs), SCMP_SYS(fstatfs), SCMP_SYS(umask),
        SCMP_SYS(uname), SCMP_SYS(sysinfo), SCMP_SYS(capget),
        SCMP_SYS(exit), SCMP_SYS(exit_group), SCMP_SYS(wait4), SCMP_SYS(waitid),
        SCMP_SYS(kill), SCMP_SYS(tgkill), SCMP_SYS(clone), SCMP_SYS(fork), SCMP_SYS(vfork),
        SCMP_SYS(execve), SCMP_SYS(fcntl), SCMP_SYS(dup), SCMP_SYS(dup2), SCMP_SYS(dup3),
        SCMP_SYS(pipe), SCMP_SYS(pipe2), SCMP_SYS(statx), SCMP_SYS(getcwd), SCMP_SYS(chdir),
        SCMP_SYS(unlink), SCMP_SYS(unlinkat), SCMP_SYS(mkdir), SCMP_SYS(mkdirat),
        SCMP_SYS(rmdir), SCMP_SYS(rename), SCMP_SYS(renameat), SCMP_SYS(renameat2),
        SCMP_SYS(chmod), SCMP_SYS(fchmod), SCMP_SYS(fchmodat), SCMP_SYS(utimensat),
        SCMP_SYS(truncate), SCMP_SYS(ftruncate), SCMP_SYS(fdatasync), SCMP_SYS(fsync),
        SCMP_SYS(getdents), SCMP_SYS(getdents64),
        SCMP_SYS(sched_yield), SCMP_SYS(sched_getaffinity), SCMP_SYS(poll), SCMP_SYS(ppoll),
        SCMP_SYS(select), SCMP_SYS(pselect6),
        SCMP_SYS(epoll_create), SCMP_SYS(epoll_create1), SCMP_SYS(epoll_ctl),
        SCMP_SYS(epoll_wait), SCMP_SYS(epoll_pwait), SCMP_SYS(eventfd), SCMP_SYS(eventfd2),
        SCMP_SYS(sched_getscheduler), SCMP_SYS(sched_getparam),
        SCMP_SYS(sched_get_priority_max), SCMP_SYS(sched_get_priority_min),
        SCMP_SYS(mincore), SCMP_SYS(getgroups), SCMP_SYS(getresuid), SCMP_SYS(getresgid),
        SCMP_SYS(getpgid), SCMP_SYS(getsid),
        SCMP_SYS(openat2), SCMP_SYS(close_range), SCMP_SYS(fadvise64), SCMP_SYS(pwrite64),
        SCMP_SYS(preadv), SCMP_SYS(pwritev), SCMP_SYS(getpriority), SCMP_SYS(setpriority)
    };
    for (size_t i = 0; i < sizeof(syscalls) / sizeof(syscalls[0]); i++) {
        if (allow_syscall(filter, syscalls[i]) < 0) {
            int saved_errno = errno;
            seccomp_release(filter);
            errno = saved_errno;
            return -1;
        }
    }
    const int denied_syscalls[] = {SCMP_SYS(socket), SCMP_SYS(socketpair), SCMP_SYS(connect),
                                   SCMP_SYS(bind), SCMP_SYS(listen), SCMP_SYS(accept), SCMP_SYS(accept4)};
    for (size_t i = 0; i < sizeof(denied_syscalls) / sizeof(denied_syscalls[0]); i++) {
        if (deny_socket_syscall(filter, denied_syscalls[i]) < 0) {
            int saved_errno = errno;
            seccomp_release(filter);
            errno = saved_errno;
            return -1;
        }
    }
#ifdef __NR_clone3
    if (deny_syscall_enosys(filter, SCMP_SYS(clone3)) < 0) {
        int saved_errno = errno;
        seccomp_release(filter);
        errno = saved_errno;
        return -1;
    }
#endif
    int rc = seccomp_load(filter);
    int saved_errno = rc < 0 ? -rc : 0;
    seccomp_release(filter);
    if (rc < 0) {
        errno = saved_errno;
        return -1;
    }
    return 0;
}

static int drop_base_privileges(int last_cap, bool proc_available) {
    if (prune_capability_bounding_set(last_cap) < 0) return -1;
    if (clear_supplementary_groups() < 0) return -1;
    if (clear_ambient_capabilities() < 0) return -1;
    if (set_unprivileged_identity() < 0) return -1;
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) return -1;
    if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) < 0) return -1;
    if ((proc_available ? verify_zero_capability_masks() : verify_zero_capability_state(last_cap)) < 0) return -1;
    return 0;
}

int phase2_target_drop_capabilities_and_identity(void) {
    int last_cap;
    if (phase2_privilege_read_cap_last_cap(&last_cap) < 0 ||
        prune_capability_bounding_set(last_cap) < 0) return -1;
    if (clear_supplementary_groups() < 0) return -1;
    if (clear_ambient_capabilities() < 0) return -1;
    if (set_unprivileged_identity() < 0) return -1;
    /* AGENT-WROTE-TIER-A: Allow cgroup pids.max (32) to bind first while providing safety ceiling at 64 */
    struct rlimit nproc_limit = {.rlim_cur = 64, .rlim_max = 64};
    if (setrlimit(RLIMIT_NPROC, &nproc_limit) < 0) return -1;
    return verify_zero_capability_masks();
}

int phase2_target_enable_nnp(void) {
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0) return -1;
    int nnp = prctl(PR_GET_NO_NEW_PRIVS, 0, 0, 0, 0);
    if (nnp != 1) {
        if (nnp >= 0) errno = EPERM;
        return -1;
    }
    return verify_zero_capability_masks();
}

int phase2_target_install_seccomp(void) {
    return install_seccomp_allowlist();
}

int phase2_supervisor_drop_privileges(int last_cap) {
    if (last_cap < 0 || last_cap > 4096) {
        errno = EINVAL;
        return -1;
    }
    if (drop_base_privileges(last_cap, false) < 0) return -1;
    return 0;
}
