#define _GNU_SOURCE

/* AGENT-WROTE-TIER-A: Phase 2 sandbox core changes are owner-authorized. */
#include "sandbox.h"
#include "phase2_cgroup.h"
#include "phase2_userns.h"
#include "phase2_rootfs.h"
#include "phase2_privilege.h"
#include <dirent.h>
#include <limits.h>
#include <signal.h>
#include <sys/syscall.h>

#define MAX_TARGET_ARGS 128U
#define MAX_TARGET_ARG_LEN 4096U

void sandbox_config_init_default(sandbox_config_t *cfg) {
    if (!cfg) return;
    cfg->job_id = "";
    cfg->timeout_ms = 2000;
    cfg->rlimit_cpu_soft = 1;
    cfg->rlimit_cpu_hard = 2;
    cfg->rlimit_as_bytes = 128ULL * 1024ULL * 1024ULL; /* 128 MiB */
    cfg->rlimit_fsize = 1024ULL * 1024ULL;              /* 1 MiB */
    cfg->rlimit_nofile = 1024;
    cfg->max_output_bytes = 64ULL * 1024ULL;            /* 64 KiB */
    cfg->target_args = NULL;
    cfg->target_argc = 0;
    cfg->stdin_data = NULL;
    cfg->stdin_len = 0;
    cfg->workspace_source_fd = -1;
    cfg->workspace_source_name = NULL;
    cfg->artifact_output_fd = -1;
    cfg->workspace_artifact_name = NULL;
}

void sandbox_result_cleanup(sandbox_result_t *res) {
    if (!res) return;
    if (res->stdout_buf) {
        free(res->stdout_buf);
        res->stdout_buf = NULL;
    }
    if (res->stderr_buf) {
        free(res->stderr_buf);
        res->stderr_buf = NULL;
    }
}

/*
 * Normalize standard file descriptors (0, 1, 2).
 * If any standard descriptor was closed before the runner was executed,
 * open /dev/null and map it to that descriptor to prevent subsequent pipe2()
 * calls from receiving low-numbered descriptors.
 */
static int normalize_standard_descriptors(void) {
    for (int fd = 0; fd <= 2; fd++) {
        if (fcntl(fd, F_GETFD) == -1 && errno == EBADF) {
            int null_fd = open("/dev/null", O_RDWR);
            if (null_fd < 0) {
                return -1;
            }
            if (null_fd != fd) {
                if (dup2(null_fd, fd) < 0) {
                    close(null_fd);
                    return -1;
                }
                close(null_fd);
            }
        }
    }
    return 0;
}

/*
 * Child error reporter:
 * Writes a binary setup_error_t record to err_pipe and exits with 127.
 */
static void child_report_error_and_exit(int err_pipe_write_fd, setup_stage_t stage, int errnum) {
    setup_error_t record;
    record.stage = (int32_t)stage;
    record.errnum = (int32_t)errnum;

    ssize_t written = 0;
    while (written < (ssize_t)sizeof(record)) {
        ssize_t n = write(err_pipe_write_fd, ((const char *)&record) + written, sizeof(record) - (size_t)written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += n;
    }
    _exit(127);
}

/* AGENT-WROTE-TIER-A: Signal forwarding handler for namespace init supervisor */
static volatile sig_atomic_t g_forward_sig = 0;

static void forward_sig_handler(int sig) {
    g_forward_sig = sig;
}

/*
 * Parent error reader:
 * Returns:
 *  1: Complete record read (sizeof(setup_error_t)) -> setup error reported.
 *  0: Clean EOF encountered with 0 bytes read -> no setup error record arrived.
 * -1: Partial record read or read error -> protocol error.
 */
static int parent_read_setup_error(int err_pipe_read_fd, setup_error_t *out_err, uint64_t deadline_ms) {
    size_t total = 0;
    char *buf = (char *)out_err;

    while (total < sizeof(setup_error_t)) {
        struct timespec now_tp;
        if (clock_gettime(CLOCK_MONOTONIC, &now_tp) < 0) return -1;
        uint64_t now_ms = (uint64_t)now_tp.tv_sec * 1000ULL + (uint64_t)now_tp.tv_nsec / 1000000ULL;
        if (now_ms >= deadline_ms) return -2;

        uint64_t remaining_ms = deadline_ms - now_ms;
        int wait_ms = remaining_ms > (uint64_t)INT_MAX ? INT_MAX : (int)remaining_ms;
        struct pollfd pfd = {.fd = err_pipe_read_fd, .events = POLLIN | POLLHUP, .revents = 0};
        int poll_ret = poll(&pfd, 1, wait_ms);
        if (poll_ret == 0) continue;
        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            return -1;
        }

        ssize_t n = read(err_pipe_read_fd, buf + total, sizeof(setup_error_t) - total);
        if (n > 0) {
            total += (size_t)n;
        } else if (n == 0) {
            break; /* EOF */
        } else {
            if (errno == EINTR) continue;
            return -1; /* Read error */
        }
    }

    if (total == sizeof(setup_error_t)) {
        return 1; /* Complete record */
    } else if (total == 0) {
        return 0; /* Clean EOF */
    } else {
        return -1; /* Partial record / protocol error */
    }
}

static pid_t wait4_retry(pid_t pid, int *status, struct rusage *usage) {
    pid_t result;
    do {
        result = wait4(pid, status, 0, usage);
    } while (result < 0 && errno == EINTR);
    return result;
}

static int terminate_and_reap_setup_child(pid_t pid, int *status, struct rusage *usage) {
    int kill_result = kill(pid, SIGKILL);
    int kill_errno = errno;
    /* Also kill the session group in case setsid() already succeeded. */
    (void)kill(-pid, SIGKILL);
    pid_t waited = wait4_retry(pid, status, usage);
    if (waited == pid) return 0;
    if (waited < 0 && errno == ECHILD) return 0;
    if (kill_result < 0 && kill_errno != ESRCH) return -1;
    return -1;
}

static void set_setup_failure(sandbox_result_t *res, setup_stage_t stage, int errnum) {
    res->status = STATUS_SETUP_ERROR;
    res->setup_err.stage = (int32_t)stage;
    res->setup_err.errnum = (int32_t)errnum;
    res->exit_code = 127;
}

static void set_elapsed_wall(sandbox_result_t *res, uint64_t start_ms) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return;
    uint64_t now_ms = (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
    res->wall_ms = (uint32_t)(now_ms - start_ms);
}

static int setup_gate_wait(int read_fd) {
    unsigned char release = 0;
    ssize_t n;
    do {
        n = read(read_fd, &release, sizeof(release));
    } while (n < 0 && errno == EINTR);
    if (n != 1 || release != 1) {
        errno = n < 0 ? errno : ECANCELED;
        return -1;
    }
    return 0;
}

static int setup_gate_release(int write_fd) {
    const unsigned char release = 1;
    ssize_t n;
    do {
        n = write(write_fd, &release, sizeof(release));
    } while (n < 0 && errno == EINTR);
    if (n != 1) {
        errno = n < 0 ? errno : EIO;
        return -1;
    }
    return 0;
}

static int parent_read_userns_ready(int read_fd, phase2_userns_ready_t *ready, uint64_t deadline_ms) {
    size_t total = 0;
    char *bytes = (char *)ready;
    while (total < sizeof(*ready)) {
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
        uint64_t now_ms = (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
        if (now_ms >= deadline_ms) return -2;
        uint64_t remaining = deadline_ms - now_ms;
        int wait_ms = remaining > (uint64_t)INT_MAX ? INT_MAX : (int)remaining;
        struct pollfd pfd = {.fd = read_fd, .events = POLLIN | POLLHUP, .revents = 0};
        int poll_result = poll(&pfd, 1, wait_ms);
        if (poll_result == 0) continue;
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        ssize_t n = read(read_fd, bytes + total, sizeof(*ready) - total);
        if (n > 0) total += (size_t)n;
        else if (n == 0) return -1;
        else if (errno != EINTR) return -1;
    }
    return 0;
}

/* AGENT-WROTE-TIER-A: Decoupled cgroup and rootfs mountpoint cleanup */
static int cleanup_job_resources(phase2_cgroup_t *cgroup, const char *root_mountpoint) {
    int result = 0;
    int saved_errno = 0;
    if (cgroup && cgroup->active) {
        if (phase2_cgroup_kill(cgroup) < 0) {
            result = -1;
            saved_errno = errno;
        } else if (phase2_cgroup_remove(cgroup) < 0) {
            result = -1;
            saved_errno = errno;
        }
    }
    if (root_mountpoint && root_mountpoint[0] != '\0' && phase2_rootfs_remove_mountpoint(root_mountpoint) < 0) {
        if (result == 0) saved_errno = errno;
        result = -1;
    }
    if (result < 0) errno = saved_errno;
    return result;
}

static int close_unrelated_fds(int preserve_fd, int preserve_fd2, int preserve_fd3, int preserve_fd4) {
#ifdef SYS_close_range
#ifdef PES_TEST_FORCE_CLOSE_RANGE_FAILURE
    /* Test-only build switch: exercise the /proc fallback deterministically. */
#else
    bool close_range_ok = true;
    int keep[4] = {preserve_fd, preserve_fd2, preserve_fd3, preserve_fd4};
    for (size_t i = 0; i < 4; i++) {
        for (size_t j = i + 1; j < 4; j++) {
            if (keep[j] >= 0 && (keep[i] < 0 || keep[j] < keep[i])) {
                int temp = keep[i]; keep[i] = keep[j]; keep[j] = temp;
            }
        }
    }
    unsigned int first = (unsigned int)(STDERR_FILENO + 1);
    for (size_t i = 0; i < 4; i++) {
        if (keep[i] < (int)first) continue;
        unsigned int current = (unsigned int)keep[i];
        if (current > first && syscall(SYS_close_range, first, current - 1U, 0) < 0) {
            if (errno != ENOSYS && errno != EINVAL) return -1;
            close_range_ok = false;
            break;
        }
        first = current + 1U;
    }
    if (close_range_ok && syscall(SYS_close_range, first, UINT_MAX, 0) < 0) {
        if (errno != ENOSYS && errno != EINVAL) return -1;
        close_range_ok = false;
    }
    if (close_range_ok) return 0;
#endif
#endif
    DIR *directory = opendir("/proc/self/fd");
    if (!directory) return -1;
    int directory_fd = dirfd(directory);
    if (directory_fd < 0) {
        int saved_errno = errno;
        if (closedir(directory) < 0) return -1;
        errno = saved_errno;
        return -1;
    }

    int result = 0;
    int saved_errno = 0;
    for (;;) {
        errno = 0;
        struct dirent *entry = readdir(directory);
        if (!entry) {
            if (errno != 0) {
                result = -1;
                saved_errno = errno;
            }
            break;
        }
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

        errno = 0;
        char *end = NULL;
        long listed_fd = strtol(entry->d_name, &end, 10);
        if (errno != 0 || end == entry->d_name || *end != '\0' || listed_fd < 0 || listed_fd > INT_MAX) {
            if (result == 0) {
                result = -1;
                saved_errno = EPROTO;
            }
            continue;
        }
        int fd = (int)listed_fd;
        if (fd <= STDERR_FILENO || fd == preserve_fd || fd == preserve_fd2 ||
            fd == preserve_fd3 || fd == preserve_fd4 || fd == directory_fd) continue;
        if (close(fd) < 0 && errno != EBADF && result == 0) {
            result = -1;
            saved_errno = errno;
        }
    }

    if (closedir(directory) < 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result < 0) errno = saved_errno;
    return result;
}

/*
 * Safe buffer append helper.
 * Appends up to n bytes from data into *buf without exceeding max_bytes.
 * Uses a temporary pointer for realloc to ensure that *buf is never overwritten
 * or lost if allocation fails.
 * Returns the number of bytes appended (>= 0), or -1 on allocation failure.
 */
static int safe_buffer_append(char **buf, size_t *len, size_t *cap, const char *data, size_t n, size_t max_bytes) {
    if (n == 0) return 0;
    size_t space = (max_bytes > *len) ? (max_bytes - *len) : 0;
    if (space == 0) return 0;
    size_t to_copy = (n < space) ? n : space;

    size_t needed = *len + to_copy;
    if (needed > *cap) {
        size_t new_cap = (*cap == 0) ? 4096 : (*cap * 2);
        while (new_cap < needed) {
            new_cap *= 2;
        }
        if (new_cap > max_bytes) {
            new_cap = max_bytes;
        }
        char *new_buf = realloc(*buf, new_cap);
        if (!new_buf) {
            /* Allocation failure: *buf is preserved */
            return -1;
        }
        *buf = new_buf;
        *cap = new_cap;
    }

    memcpy(*buf + *len, data, to_copy);
    *len += to_copy;
    return (int)to_copy;
}

/*
 * Bounded non-blocking pipe drain helper.
 * Reads available bytes from pipe_fd into *buf up to max_bytes.
 * Once kernel pipe buffer is empty (EAGAIN/EWOULDBLOCK) or EOF is reached, closes pipe_fd.
 * Never blocks waiting for uncooperative descendants.
 */
static void drain_and_close_pipe(int pipe_fd, char **buf, size_t *len, size_t *cap, size_t max_bytes, bool *truncated) {
    if (pipe_fd < 0) return;
    char chunk[4096];

    while (true) {
        size_t space = (max_bytes > *len) ? (max_bytes - *len) : 0;
        if (space > 0) {
            size_t to_read = sizeof(chunk);
            if (to_read > space) to_read = space;
            ssize_t n = read(pipe_fd, chunk, to_read);
            if (n > 0) {
                int written = safe_buffer_append(buf, len, cap, chunk, (size_t)n, max_bytes);
                if (written < 0) {
                    /* On allocation failure during drain, stop appending and drain remainder into sink */
                    *truncated = true;
                    break;
                }
                if ((size_t)written < (size_t)n) {
                    *truncated = true;
                    break;
                }
            } else if (n == 0) {
                break; /* EOF */
            } else {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break; /* Read error */
            }
        } else {
            *truncated = true;
            ssize_t n = read(pipe_fd, chunk, sizeof(chunk));
            if (n <= 0) {
                if (n < 0 && errno == EINTR) continue;
                break;
            }
        }
    }
    close(pipe_fd);
}

#ifdef PES_TEST_FAULT_INJECTION
/* AGENT-WROTE-TIER-A: Test-only fault injection hooks to exercise error branches deterministically */
static int pes_test_inject_cgroup_create_errno = 0;
static bool pes_test_inject_cgroup_create_fake_success = false;
static int pes_test_inject_fork_errno = 0;
static char pes_test_last_root_mountpoint[PATH_MAX] = {0};
#endif

int run_sandbox_with_config(const char *binary_path, const sandbox_config_t *cfg, sandbox_result_t *res) {
    if (!binary_path || !cfg || !res) {
        return -1;
    }
    memset(res, 0, sizeof(*res));
    if (cfg->target_argc > MAX_TARGET_ARGS || (cfg->target_argc > 0 && !cfg->target_args)) {
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        errno = E2BIG;
        return -1;
    }
    if (cfg->stdin_len > PES_MAX_STDIN_BYTES || (cfg->stdin_len > 0 && !cfg->stdin_data) ||
        ((cfg->workspace_source_fd < 0) != (cfg->workspace_source_name == NULL)) ||
        (cfg->artifact_output_fd >= 0 && cfg->workspace_source_fd < 0)) {
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        errno = EINVAL;
        return -1;
    }
    for (size_t i = 0; i < cfg->target_argc; i++) {
        if (!cfg->target_args[i] || strnlen(cfg->target_args[i], MAX_TARGET_ARG_LEN + 1U) > MAX_TARGET_ARG_LEN) {
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            errno = E2BIG;
            return -1;
        }
    }

    /* Ensure descriptors 0, 1, 2 are valid */
    if (normalize_standard_descriptors() < 0) {
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    /* Create CLOEXEC error pipe */
    int err_pipe[2];
    if (pipe2(err_pipe, O_CLOEXEC) < 0) {
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    /* Create CLOEXEC stdout pipe */
    int stdout_pipe[2];
    if (pipe2(stdout_pipe, O_CLOEXEC) < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    /* Create CLOEXEC stderr pipe */
    int stderr_pipe[2];
    if (pipe2(stderr_pipe, O_CLOEXEC) < 0) {
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    /* Set read ends of stdout and stderr to non-blocking */
    if (fcntl(stdout_pipe[0], F_SETFL, O_NONBLOCK) < 0 ||
        fcntl(stderr_pipe[0], F_SETFL, O_NONBLOCK) < 0) {
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    int gate_pipe[2];
    if (pipe2(gate_pipe, O_CLOEXEC) < 0) {
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    int userns_ready_pipe[2];
    if (pipe2(userns_ready_pipe, O_CLOEXEC) < 0) {
        close(gate_pipe[0]); close(gate_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    int placement_pipe[2];
    if (pipe2(placement_pipe, O_CLOEXEC) < 0) {
        close(userns_ready_pipe[0]); close(userns_ready_pipe[1]);
        close(gate_pipe[0]); close(gate_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    int artifact_status_pipe[2] = {-1, -1};
    if (cfg->artifact_output_fd >= 0 && pipe2(artifact_status_pipe, O_CLOEXEC) < 0) {
        close(userns_ready_pipe[0]); close(userns_ready_pipe[1]);
        close(gate_pipe[0]); close(gate_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        close(placement_pipe[0]); close(placement_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    phase2_cgroup_t job_cgroup;
    memset(&job_cgroup, 0, sizeof(job_cgroup));
    phase2_cgroup_limits_t cgroup_limits;
    phase2_cgroup_limits_init(&cgroup_limits);
#ifdef PES_TEST_FAULT_INJECTION
    int cgroup_create_res = 0;
    if (pes_test_inject_cgroup_create_errno != 0) {
        cgroup_create_res = -1;
        errno = pes_test_inject_cgroup_create_errno;
    } else if (pes_test_inject_cgroup_create_fake_success) {
        cgroup_create_res = 0;
    } else {
        cgroup_create_res = phase2_cgroup_create(&job_cgroup, "/sys/fs/cgroup", "/proc/self/cgroup", &cgroup_limits);
    }
#else
    int cgroup_create_res = phase2_cgroup_create(&job_cgroup, "/sys/fs/cgroup", "/proc/self/cgroup", &cgroup_limits);
#endif
    if (cgroup_create_res < 0) {
        int setup_errno = errno;
        if (job_cgroup.active && cleanup_job_resources(&job_cgroup, NULL) < 0) {
            close(userns_ready_pipe[0]); close(userns_ready_pipe[1]);
            close(placement_pipe[0]); close(placement_pipe[1]);
            close(gate_pipe[0]); close(gate_pipe[1]);
            close(err_pipe[0]); close(err_pipe[1]);
            close(stdout_pipe[0]); close(stdout_pipe[1]);
            close(stderr_pipe[0]); close(stderr_pipe[1]);
            if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
            if (artifact_status_pipe[1] >= 0) close(artifact_status_pipe[1]);
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            return -1;
        }
        close(userns_ready_pipe[0]); close(userns_ready_pipe[1]);
        close(placement_pipe[0]); close(placement_pipe[1]);
        close(gate_pipe[0]); close(gate_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        if (artifact_status_pipe[1] >= 0) close(artifact_status_pipe[1]);
        set_setup_failure(res, SETUP_STAGE_CGROUP_CREATE, setup_errno);
        return 0;
    }

    char root_mountpoint[PATH_MAX] = {0};
    if (phase2_rootfs_create_mountpoint(root_mountpoint, sizeof(root_mountpoint)) < 0) {
        int setup_errno = errno;
        (void)cleanup_job_resources(&job_cgroup, NULL);
        close(userns_ready_pipe[0]); close(userns_ready_pipe[1]);
        close(placement_pipe[0]); close(placement_pipe[1]);
        close(gate_pipe[0]); close(gate_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        if (artifact_status_pipe[1] >= 0) close(artifact_status_pipe[1]);
        set_setup_failure(res, SETUP_STAGE_ROOTFS, setup_errno);
        return 0;
    }
#ifdef PES_TEST_FAULT_INJECTION
    snprintf(pes_test_last_root_mountpoint, sizeof(pes_test_last_root_mountpoint), "%s", root_mountpoint);
#endif

    /* Sample start monotonic time */
    struct timespec start_tp;
    clock_gettime(CLOCK_MONOTONIC, &start_tp);
    uint64_t start_ms = (uint64_t)start_tp.tv_sec * 1000ULL + (uint64_t)start_tp.tv_nsec / 1000000ULL;
    uint64_t deadline_ms = start_ms + (uint64_t)cfg->timeout_ms;
    pid_t runner_pid = getpid();

#ifdef PES_TEST_FAULT_INJECTION
    pid_t pid = pes_test_inject_fork_errno != 0 ?
        (errno = pes_test_inject_fork_errno, (pid_t)-1) :
        fork();
#else
    pid_t pid = fork();
#endif
    if (pid < 0) {
        int fork_errno = errno;
        (void)cleanup_job_resources(&job_cgroup, root_mountpoint);
        close(userns_ready_pipe[0]); close(userns_ready_pipe[1]);
        close(placement_pipe[0]); close(placement_pipe[1]);
        close(gate_pipe[0]); close(gate_pipe[1]);
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        if (artifact_status_pipe[1] >= 0) close(artifact_status_pipe[1]);
        set_setup_failure(res, SETUP_STAGE_SUPERVISOR, fork_errno);
        return 0;
    }

    if (pid == 0) {
        /*
         * --- CHILD PROCESS ---
         */
        close(err_pipe[0]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        close(gate_pipe[1]);
        close(userns_ready_pipe[0]);
        close(placement_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);

        if (setup_gate_wait(placement_pipe[0]) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_CGROUP_RELEASE, errno);
        }
        close(placement_pipe[0]);

        if (phase2_set_parent_death_signal(runner_pid) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, errno);
        }

        if (phase2_userns_child_start(userns_ready_pipe[1]) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_USERNS_CREATE, errno);
        }
        close(userns_ready_pipe[1]);

        if (setup_gate_wait(gate_pipe[0]) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_CGROUP_RELEASE, errno);
        }

        if (dup2(gate_pipe[0], STDIN_FILENO) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_CLOSE_FDS, errno);
        }
        if (gate_pipe[0] != STDIN_FILENO && close(gate_pipe[0]) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_CLOSE_FDS, errno);
        }
        /* 1. Establish new session / process group */
        if (setsid() < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SETSID, errno);
        }

        /* 2. Redirect stdout and stderr */
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_DUP2_STDOUT, errno);
        }
        if (dup2(stderr_pipe[1], STDERR_FILENO) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_DUP2_STDERR, errno);
        }

        /* Close original write descriptors if not already 1 and 2 */
        if (stdout_pipe[1] != STDOUT_FILENO) close(stdout_pipe[1]);
        if (stderr_pipe[1] != STDERR_FILENO) close(stderr_pipe[1]);
        if (close_unrelated_fds(err_pipe[1], cfg->workspace_source_fd, cfg->artifact_output_fd,
                                artifact_status_pipe[1]) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_CLOSE_FDS, errno);
        }

        /* 3. Apply resource limits (rlimits) */
        struct rlimit rl;

        /* RLIMIT_CPU */
        rl.rlim_cur = cfg->rlimit_cpu_soft;
        rl.rlim_max = cfg->rlimit_cpu_hard;
        if (setrlimit(RLIMIT_CPU, &rl) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SETRLIMIT_CPU, errno);
        }

        /* RLIMIT_AS (Virtual Address Space) */
        rl.rlim_cur = cfg->rlimit_as_bytes;
        rl.rlim_max = cfg->rlimit_as_bytes;
        if (setrlimit(RLIMIT_AS, &rl) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SETRLIMIT_AS, errno);
        }

        /* RLIMIT_FSIZE (Maximum File Size) */
        rl.rlim_cur = cfg->rlimit_fsize;
        rl.rlim_max = cfg->rlimit_fsize;
        if (setrlimit(RLIMIT_FSIZE, &rl) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SETRLIMIT_FSIZE, errno);
        }

        /* RLIMIT_CORE (0 = suppress core dumps) */
        rl.rlim_cur = 0;
        rl.rlim_max = 0;
        if (setrlimit(RLIMIT_CORE, &rl) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SETRLIMIT_CORE, errno);
        }

        /* RLIMIT_NOFILE */
        rl.rlim_cur = cfg->rlimit_nofile;
        rl.rlim_max = cfg->rlimit_nofile;
        if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SETRLIMIT_NOFILE, errno);
        }

        if (phase2_child_create_namespaces() < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_NAMESPACES, errno);
        }

        int supervisor_last_cap;
        if (phase2_privilege_read_cap_last_cap(&supervisor_last_cap) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_CAPABILITY_DROP, errno);
        }

        char target_path[PATH_MAX];
        if (phase2_rootfs_prepare(binary_path, root_mountpoint, target_path, sizeof(target_path),
                                  cfg->workspace_source_fd, cfg->workspace_source_name, 65534, 65534) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_ROOTFS, errno);
        }
        if (cfg->workspace_source_fd >= 0) {
            if (close(cfg->workspace_source_fd) < 0) {
                child_report_error_and_exit(err_pipe[1], SETUP_STAGE_WORKSPACE, errno);
            }
        }

        /* AGENT-WROTE-TIER-A: Dedicated pipe for Child 2 -> Child 1 status propagation */
        int status_pipe[2];
        if (pipe2(status_pipe, O_CLOEXEC) < 0) {
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, errno);
        }

        pid_t target_pid = fork();
        if (target_pid < 0) {
            int fork_err = errno;
            close(status_pipe[0]);
            close(status_pipe[1]);
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_NAMESPACES, fork_err);
        }
        if (target_pid == 0) {
            /*
             * --- CHILD 2: Namespace Init Supervisor (PID 1 in PID namespace) ---
             */
            close(status_pipe[0]);

            /* Child 2 is PID 1 here; its parent is outside this PID namespace, so getppid() is 0. */
            if (phase2_set_parent_death_signal(0) < 0) {
                close(status_pipe[1]);
                child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, errno);
            }
            if (phase2_rootfs_mount_proc() < 0) {
                close(status_pipe[1]);
                child_report_error_and_exit(err_pipe[1], SETUP_STAGE_PROCFS, errno);
            }

            pid_t app_pid = fork();
            if (app_pid < 0) {
                int app_fork_err = errno;
                close(status_pipe[1]);
                child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, app_fork_err);
            }

            if (app_pid == 0) {
                /*
                 * --- CHILD 3: Untrusted Target (PID 2 in PID namespace) ---
                 */
                close(status_pipe[1]);

                /* Child 3's parent is Child 2 (PID 1 in this namespace). */
                if (phase2_set_parent_death_signal(1) < 0) {
                    child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, errno);
                }
                if (phase2_target_drop_capabilities_and_identity() < 0) {
                    child_report_error_and_exit(err_pipe[1], SETUP_STAGE_CAPABILITY_DROP, errno);
                }
                /* Credential changes clear PDEATHSIG; reinstall it with expected parent PID 1. */
                if (phase2_set_parent_death_signal(1) < 0) {
                    child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, errno);
                }
                if (phase2_target_enable_nnp() < 0) {
                    child_report_error_and_exit(err_pipe[1], SETUP_STAGE_NNP, errno);
                }
                if (phase2_target_install_seccomp() < 0) {
                    child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SECCOMP, errno);
                }

                /* Reset signal mask and restore default dispositions for standard signals */
                sigset_t empty_mask;
                sigemptyset(&empty_mask);
                if (sigprocmask(SIG_SETMASK, &empty_mask, NULL) < 0) {
                    child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, errno);
                }
                for (int s = 1; s < 32; s++) {
                    if (s == SIGKILL || s == SIGSTOP) continue;
                    (void)signal(s, SIG_DFL);
                }

                char *child_argv[MAX_TARGET_ARGS + 2];
                child_argv[0] = target_path;
                for (size_t arg_index = 0; arg_index < cfg->target_argc; arg_index++) {
                    child_argv[arg_index + 1] = (char *)cfg->target_args[arg_index];
                }
                child_argv[cfg->target_argc + 1] = NULL;
                char *const child_envp[] = { "PATH=/usr/bin:/bin", NULL };
                execve(target_path, child_argv, child_envp);
                child_report_error_and_exit(err_pipe[1], SETUP_STAGE_EXECVE, errno);
            }

            /*
             * --- CHILD 2 CONTINUED (Supervisor, PID 1 in PID namespace) ---
             */
            close(STDOUT_FILENO);
            close(STDERR_FILENO);

            if (phase2_supervisor_drop_privileges(supervisor_last_cap) < 0) {
                int supervisor_errno = errno;
                (void)kill(app_pid, SIGKILL);
                int tmp_status;
                (void)waitpid(app_pid, &tmp_status, 0);
                close(status_pipe[1]);
                child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, supervisor_errno);
            }

            /* Close error pipe: target (Child 3) is now the only writer */
            close(err_pipe[1]);

            struct sigaction sa;
            memset(&sa, 0, sizeof(sa));
            sa.sa_handler = forward_sig_handler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = 0;
            (void)sigaction(SIGTERM, &sa, NULL);
            (void)sigaction(SIGINT, &sa, NULL);
            (void)sigaction(SIGHUP, &sa, NULL);
            (void)sigaction(SIGQUIT, &sa, NULL);

            int app_status = 0;
            bool app_reaped = false;

            while (true) {
                if (g_forward_sig != 0) {
                    int sig_to_fwd = g_forward_sig;
                    g_forward_sig = 0;
                    (void)kill(-1, sig_to_fwd);
                }

                int status = 0;
                pid_t waited = waitpid(-1, &status, 0);
                if (waited < 0) {
                    if (errno == EINTR) {
                        continue;
                    }
                    if (errno == ECHILD) {
                        break;
                    }
                    break;
                }

                if (waited == app_pid) {
                    app_status = status;
                    app_reaped = true;
                    (void)kill(-1, SIGKILL);
                }
            }

            if (app_reaped) {
                size_t written = 0;
                while (written < sizeof(app_status)) {
                    ssize_t w = write(status_pipe[1], ((const char *)&app_status) + written, sizeof(app_status) - written);
                    if (w > 0) {
                        written += (size_t)w;
                    } else if (w < 0 && errno == EINTR) {
                        continue;
                    } else {
                        break;
                    }
                }
            }

            close(status_pipe[1]);
            _exit(app_reaped ? 0 : 126);
        }

        /*
         * --- CHILD 1 (Host-level intermediate supervisor outside PID namespace) ---
         */
        close(status_pipe[1]);
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);

        if (phase2_supervisor_drop_privileges(supervisor_last_cap) < 0) {
            int supervisor_errno = errno;
            close(status_pipe[0]);
            (void)kill(target_pid, SIGKILL);
            int child2_status = 0;
            struct rusage child2_usage;
            memset(&child2_usage, 0, sizeof(child2_usage));
            (void)wait4_retry(target_pid, &child2_status, &child2_usage);
            child_report_error_and_exit(err_pipe[1], SETUP_STAGE_SUPERVISOR, supervisor_errno);
        }

        close(err_pipe[1]);

        int target_status = 0;
        size_t total_status_read = 0;
        while (total_status_read < sizeof(target_status)) {
            ssize_t n = read(status_pipe[0], ((char *)&target_status) + total_status_read, sizeof(target_status) - total_status_read);
            if (n > 0) {
                total_status_read += (size_t)n;
            } else if (n == 0) {
                break;
            } else {
                if (errno == EINTR) continue;
                break;
            }
        }
        close(status_pipe[0]);

        int child2_status = 0;
        pid_t waited;
        do {
            waited = waitpid(target_pid, &child2_status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited != target_pid) _exit(126);

        int final_status = (total_status_read == sizeof(target_status)) ? target_status : child2_status;

        if (cfg->artifact_output_fd >= 0) {
            unsigned char artifact_status = 0;
            if (WIFEXITED(final_status) && WEXITSTATUS(final_status) == 0) {
                int workspace_fd = open("/workspace", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
                const char *art_leaf = cfg->workspace_artifact_name ? cfg->workspace_artifact_name : "target";
                if (workspace_fd >= 0 && phase2_rootfs_copy_artifact(
                        workspace_fd, art_leaf, cfg->artifact_output_fd, 65534,
                        (size_t)cfg->rlimit_fsize) == 0) {
                    artifact_status = 1;
                } else {
                    artifact_status = 2;
                }
                if (workspace_fd >= 0) close(workspace_fd);
            }
            ssize_t status_written;
            do {
                status_written = write(artifact_status_pipe[1], &artifact_status, sizeof(artifact_status));
            } while (status_written < 0 && errno == EINTR);
            if (status_written != (ssize_t)sizeof(artifact_status) || artifact_status == 2) _exit(126);
            close(artifact_status_pipe[1]);
        }

        if (WIFEXITED(final_status)) {
            _exit(WEXITSTATUS(final_status));
        }
        if (WIFSIGNALED(final_status)) {
            int target_signal = WTERMSIG(final_status);
            (void)signal(target_signal, SIG_DFL);
            (void)raise(target_signal);
            _exit(128 + target_signal);
        }
        _exit(126);
    }

    /*
     * --- PARENT PROCESS ---
     */
    close(err_pipe[1]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    close(gate_pipe[0]);
    close(userns_ready_pipe[1]);
    close(placement_pipe[0]);
    if (artifact_status_pipe[1] >= 0) close(artifact_status_pipe[1]);

    if (phase2_cgroup_attach(&job_cgroup, pid) < 0) {
        int attach_errno = errno;
        int child_status = 0;
        struct rusage child_usage;
        memset(&child_usage, 0, sizeof(child_usage));
        (void)terminate_and_reap_setup_child(pid, &child_status, &child_usage);
        close(gate_pipe[1]); close(userns_ready_pipe[0]); close(placement_pipe[1]); close(err_pipe[0]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        if (cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            return -1;
        }
        set_setup_failure(res, SETUP_STAGE_CGROUP_ATTACH, attach_errno);
        return 0;
    }

    if (setup_gate_release(placement_pipe[1]) < 0) {
        int placement_errno = errno;
        int child_status = 0;
        struct rusage child_usage;
        memset(&child_usage, 0, sizeof(child_usage));
        int cleanup_result = terminate_and_reap_setup_child(pid, &child_status, &child_usage);
        close(placement_pipe[1]); close(userns_ready_pipe[0]); close(gate_pipe[1]); close(err_pipe[0]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        if (cleanup_result < 0 || cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_CHILD_LOST;
            return -1;
        }
        set_setup_failure(res, SETUP_STAGE_CGROUP_RELEASE, placement_errno);
        return 0;
    }
    close(placement_pipe[1]);

    phase2_userns_ready_t userns_ready = {0};
    int ready_result = parent_read_userns_ready(userns_ready_pipe[0], &userns_ready, deadline_ms);
    close(userns_ready_pipe[0]);
    if (ready_result != 0 || userns_ready.result != 0) {
        int child_status = 0;
        struct rusage child_usage;
        memset(&child_usage, 0, sizeof(child_usage));
        int cleanup_result = terminate_and_reap_setup_child(pid, &child_status, &child_usage);
        close(gate_pipe[1]); close(err_pipe[0]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        if (cleanup_result < 0 || cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_CHILD_LOST;
            return -1;
        }
        if (ready_result == -2) {
            res->status = STATUS_TIMEOUT;
            set_elapsed_wall(res, start_ms);
            return 0;
        }
        set_setup_failure(res, SETUP_STAGE_USERNS_CREATE,
                          userns_ready.errnum != 0 ? userns_ready.errnum : EPROTO);
        return 0;
    }

    bool gid_map_failed = false;
    if (phase2_userns_parent_map(pid, deadline_ms, &gid_map_failed) < 0) {
        int map_errno = errno;
        int child_status = 0;
        struct rusage child_usage;
        memset(&child_usage, 0, sizeof(child_usage));
        int cleanup_result = terminate_and_reap_setup_child(pid, &child_status, &child_usage);
        close(gate_pipe[1]); close(err_pipe[0]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        if (cleanup_result < 0 || cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_CHILD_LOST;
            return -1;
        }
        if (map_errno == ETIMEDOUT) {
            res->status = STATUS_TIMEOUT;
            set_elapsed_wall(res, start_ms);
            return 0;
        }
        set_setup_failure(res, gid_map_failed ? SETUP_STAGE_GID_MAP : SETUP_STAGE_UID_MAP, map_errno);
        return 0;
    }

    if (setup_gate_release(gate_pipe[1]) < 0) {
        int release_errno = errno;
        int child_status = 0;
        struct rusage child_usage;
        memset(&child_usage, 0, sizeof(child_usage));
        (void)terminate_and_reap_setup_child(pid, &child_status, &child_usage);
        close(gate_pipe[1]); close(err_pipe[0]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        close(stdout_pipe[0]); close(stderr_pipe[0]);
        if (cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            return -1;
        }
        set_setup_failure(res, SETUP_STAGE_CGROUP_RELEASE, release_errno);
        return 0;
    }
    /* Read error pipe */
    setup_error_t s_err;
    memset(&s_err, 0, sizeof(s_err));
    int err_res = parent_read_setup_error(err_pipe[0], &s_err, deadline_ms);
    close(err_pipe[0]);

    if (err_res == 1) {
        /* Child explicitly reported setup/exec failure */
        int child_status = 0;
        struct rusage child_usage;
        pid_t reap_pid = wait4_retry(pid, &child_status, &child_usage);

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        close(gate_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);

        if (reap_pid != pid) {
            (void)cleanup_job_resources(&job_cgroup, root_mountpoint);
            res->status = STATUS_RUNNER_CHILD_LOST;
            return -1;
        }

        if (cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            return -1;
        }

        res->status = STATUS_SETUP_ERROR;
        res->setup_err = s_err;
        res->exit_code = 127;
        return 0;
    } else if (err_res == -1 || err_res == -2) {
        /* A broken handshake or expired deadline must not enter the run loop. */
        int child_status = 0;
        struct rusage child_usage;
        memset(&child_usage, 0, sizeof(child_usage));
        int cleanup_result = terminate_and_reap_setup_child(pid, &child_status, &child_usage);

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        close(gate_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);

        if (cleanup_result < 0 || cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_CHILD_LOST;
            return -1;
        }

        if (err_res == -2) {
            struct timespec end_tp;
            if (clock_gettime(CLOCK_MONOTONIC, &end_tp) == 0) {
                uint64_t end_ms = (uint64_t)end_tp.tv_sec * 1000ULL + (uint64_t)end_tp.tv_nsec / 1000000ULL;
                res->wall_ms = (uint32_t)(end_ms - start_ms);
            }
            res->cpu_ms = (uint32_t)(
                (uint64_t)child_usage.ru_utime.tv_sec * 1000ULL + (uint64_t)child_usage.ru_utime.tv_usec / 1000ULL +
                (uint64_t)child_usage.ru_stime.tv_sec * 1000ULL + (uint64_t)child_usage.ru_stime.tv_usec / 1000ULL
            );
            res->peak_memory_kb = (uint64_t)child_usage.ru_maxrss;
            res->status = STATUS_TIMEOUT;
            return 0;
        }

        res->status = STATUS_SETUP_PROTOCOL_ERROR;
        res->exit_code = 127;
        return 0;
    }

    /*
     * err_res == 0 (EOF): No setup error record arrived.
     * Enter poll-based event loop with continuous direct-child liveness checking.
     */
    bool child_reaped = false;
    int child_status = 0;
    struct rusage child_usage;
    memset(&child_usage, 0, sizeof(child_usage));

    bool timed_out = false;
    bool output_cap_breached = false;
    bool alloc_failed = false;
    bool stdout_open = true;
    bool stderr_open = true;
    bool stdin_open = cfg->stdin_len > 0;
    size_t stdin_offset = 0;
    if (!stdin_open) {
        close(gate_pipe[1]);
        gate_pipe[1] = -1;
    } else if (fcntl(gate_pipe[1], F_SETFL, O_NONBLOCK) < 0) {
        int setup_errno = errno;
        (void)terminate_and_reap_setup_child(pid, &child_status, &child_usage);
        close(gate_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        if (cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            return -1;
        }
        set_setup_failure(res, SETUP_STAGE_SUPERVISOR, setup_errno);
        return 0;
    }

    struct sigaction ignore_sigpipe;
    struct sigaction previous_sigpipe;
    memset(&ignore_sigpipe, 0, sizeof(ignore_sigpipe));
    ignore_sigpipe.sa_handler = SIG_IGN;
    sigemptyset(&ignore_sigpipe.sa_mask);
    if (sigaction(SIGPIPE, &ignore_sigpipe, &previous_sigpipe) < 0) {
        int setup_errno = errno;
        (void)terminate_and_reap_setup_child(pid, &child_status, &child_usage);
        if (stdin_open) close(gate_pipe[1]);
        if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        if (cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            return -1;
        }
        set_setup_failure(res, SETUP_STAGE_SUPERVISOR, setup_errno);
        return 0;
    }

    while (!child_reaped) {
        /* Check monotonic wall clock before poll */
        struct timespec pre_poll_tp;
        clock_gettime(CLOCK_MONOTONIC, &pre_poll_tp);
        uint64_t pre_poll_ms = (uint64_t)pre_poll_tp.tv_sec * 1000ULL + (uint64_t)pre_poll_tp.tv_nsec / 1000000ULL;

        if (pre_poll_ms >= deadline_ms) {
            timed_out = true;
            break;
        }

        uint64_t remaining_ms = deadline_ms - pre_poll_ms;
        int poll_timeout = (remaining_ms > 50ULL) ? 50 : (int)remaining_ms;

        if (stdout_open || stderr_open || stdin_open) {
            struct pollfd pfds[3];
            int nfds = 0;
            int stdout_idx = -1, stderr_idx = -1, stdin_idx = -1;

            if (stdout_open) {
                stdout_idx = nfds++;
                pfds[stdout_idx].fd = stdout_pipe[0];
                pfds[stdout_idx].events = POLLIN | POLLHUP;
                pfds[stdout_idx].revents = 0;
            }
            if (stderr_open) {
                stderr_idx = nfds++;
                pfds[stderr_idx].fd = stderr_pipe[0];
                pfds[stderr_idx].events = POLLIN | POLLHUP;
                pfds[stderr_idx].revents = 0;
            }
            if (stdin_open) {
                stdin_idx = nfds++;
                pfds[stdin_idx].fd = gate_pipe[1];
                pfds[stdin_idx].events = POLLOUT | POLLHUP | POLLERR;
                pfds[stdin_idx].revents = 0;
            }

            int poll_ret = poll(pfds, (nfds_t)nfds, poll_timeout);
            if (poll_ret < 0) {
                if (errno == EINTR) continue;
                /* Fatal poll error: perform complete cleanup */
                kill(-pid, SIGKILL);
                if (!child_reaped) {
                    pid_t reap_p;
                    do {
                        reap_p = wait4(pid, &child_status, 0, &child_usage);
                    } while (reap_p == -1 && errno == EINTR);
                    child_reaped = (reap_p == pid);
                }
                if (stdout_open) { close(stdout_pipe[0]); stdout_open = false; }
                if (stderr_open) { close(stderr_pipe[0]); stderr_open = false; }
                if (stdin_open) { close(gate_pipe[1]); gate_pipe[1] = -1; stdin_open = false; }
                res->status = STATUS_RUNNER_INTERNAL_ERROR;
                break;
            }

            /* Drain stdout if ready */
            if (stdout_open && (pfds[stdout_idx].revents & (POLLIN | POLLHUP))) {
                char chunk[4096];
                while (true) {
                    ssize_t n = read(stdout_pipe[0], chunk, sizeof(chunk));
                    if (n > 0) {
                        if (res->stdout_len + (size_t)n > cfg->max_output_bytes) {
                            size_t keep = cfg->max_output_bytes - res->stdout_len;
                            if (keep > 0) {
                                int appended = safe_buffer_append(&res->stdout_buf, &res->stdout_len, &res->stdout_cap, chunk, keep, cfg->max_output_bytes);
                                if (appended < 0) {
                                    alloc_failed = true;
                                    break;
                                }
                            }
                            res->stdout_truncated = true;
                            output_cap_breached = true;
                            break;
                        } else {
                            int appended = safe_buffer_append(&res->stdout_buf, &res->stdout_len, &res->stdout_cap, chunk, (size_t)n, cfg->max_output_bytes);
                            if (appended < 0) {
                                alloc_failed = true;
                                break;
                            }
                        }
                    } else if (n == 0) {
                        close(stdout_pipe[0]);
                        stdout_open = false;
                        break;
                    } else {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        close(stdout_pipe[0]);
                        stdout_open = false;
                        break;
                    }
                }
                if (alloc_failed || output_cap_breached) break;
            }

            /* Drain stderr if ready */
            if (stderr_open && (pfds[stderr_idx].revents & (POLLIN | POLLHUP))) {
                char chunk[4096];
                while (true) {
                    ssize_t n = read(stderr_pipe[0], chunk, sizeof(chunk));
                    if (n > 0) {
                        if (res->stderr_len + (size_t)n > cfg->max_output_bytes) {
                            size_t keep = cfg->max_output_bytes - res->stderr_len;
                            if (keep > 0) {
                                int appended = safe_buffer_append(&res->stderr_buf, &res->stderr_len, &res->stderr_cap, chunk, keep, cfg->max_output_bytes);
                                if (appended < 0) {
                                    alloc_failed = true;
                                    break;
                                }
                            }
                            res->stderr_truncated = true;
                            output_cap_breached = true;
                            break;
                        } else {
                            int appended = safe_buffer_append(&res->stderr_buf, &res->stderr_len, &res->stderr_cap, chunk, (size_t)n, cfg->max_output_bytes);
                            if (appended < 0) {
                                alloc_failed = true;
                                break;
                            }
                        }
                    } else if (n == 0) {
                        close(stderr_pipe[0]);
                        stderr_open = false;
                        break;
                    } else {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        close(stderr_pipe[0]);
                        stderr_open = false;
                        break;
                    }
                }
                if (alloc_failed || output_cap_breached) break;
            }
            if (stdin_open && (pfds[stdin_idx].revents & (POLLOUT | POLLHUP | POLLERR))) {
                if (pfds[stdin_idx].revents & (POLLHUP | POLLERR)) {
                    close(gate_pipe[1]);
                    gate_pipe[1] = -1;
                    stdin_open = false;
                } else {
                    ssize_t written = write(gate_pipe[1], cfg->stdin_data + stdin_offset,
                                            cfg->stdin_len - stdin_offset);
                    if (written > 0) {
                        stdin_offset += (size_t)written;
                        if (stdin_offset == cfg->stdin_len) {
                            close(gate_pipe[1]);
                            gate_pipe[1] = -1;
                            stdin_open = false;
                        }
                    } else if (written < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                        close(gate_pipe[1]);
                        gate_pipe[1] = -1;
                        stdin_open = false;
                    }
                }
            }
        } else {
            /* Both pipes closed: sleep briefly while respecting deadline */
            (void)poll(NULL, 0, poll_timeout);
        }

        if (alloc_failed) {
            kill(-pid, SIGKILL);
            if (!child_reaped) {
                pid_t reap_p;
                do {
                    reap_p = wait4(pid, &child_status, 0, &child_usage);
                } while (reap_p == -1 && errno == EINTR);
                child_reaped = (reap_p == pid);
            }
            if (stdout_open) { close(stdout_pipe[0]); stdout_open = false; }
            if (stderr_open) { close(stderr_pipe[0]); stderr_open = false; }
            if (stdin_open) { close(gate_pipe[1]); gate_pipe[1] = -1; stdin_open = false; }
            if (cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
                (void)sigaction(SIGPIPE, &previous_sigpipe, NULL);
                if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
                res->status = STATUS_RUNNER_INTERNAL_ERROR;
                return -1;
            }
            (void)sigaction(SIGPIPE, &previous_sigpipe, NULL);
            if (artifact_status_pipe[0] >= 0) close(artifact_status_pipe[0]);
            res->status = STATUS_RUNNER_INTERNAL_ERROR;
            return -1;
        }

        if (output_cap_breached) {
            break;
        }

        /* Check monotonic wall clock post-poll */
        struct timespec post_poll_tp;
        clock_gettime(CLOCK_MONOTONIC, &post_poll_tp);
        uint64_t post_poll_ms = (uint64_t)post_poll_tp.tv_sec * 1000ULL + (uint64_t)post_poll_tp.tv_nsec / 1000000ULL;
        if (post_poll_ms >= deadline_ms) {
            timed_out = true;
            break;
        }

        /* Check direct-child status via non-blocking wait4 */
        pid_t reap_pid = wait4(pid, &child_status, WNOHANG, &child_usage);
        if (reap_pid == pid) {
            /*
             * Child exit observed.
             * Tie-breaker rule: sample monotonic clock at exact observation moment.
             * If observed at or after deadline, timeout deterministically wins because
             * POSIX wait4 reports reap time, not precise kernel exit time.
             */
            struct timespec obs_tp;
            clock_gettime(CLOCK_MONOTONIC, &obs_tp);
            uint64_t obs_ms = (uint64_t)obs_tp.tv_sec * 1000ULL + (uint64_t)obs_tp.tv_nsec / 1000000ULL;
            child_reaped = true;
            if (obs_ms >= deadline_ms) {
                timed_out = true;
            }
            break;
        } else if (reap_pid == 0) {
            /* Child still running, continue loop */
        } else if (reap_pid == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ECHILD) {
                /* Child already reaped or not waitable */
                child_reaped = true;
                kill(-pid, SIGKILL);
                if (stdout_open) { close(stdout_pipe[0]); stdout_open = false; }
                if (stderr_open) { close(stderr_pipe[0]); stderr_open = false; }
                res->status = STATUS_RUNNER_CHILD_LOST;
                break;
            } else {
                /* Fatal wait4 error: signal process group, reap direct child, close fds */
                kill(-pid, SIGKILL);
                if (!child_reaped) {
                    pid_t reap_p;
                    do {
                        reap_p = wait4(pid, &child_status, 0, &child_usage);
                    } while (reap_p == -1 && errno == EINTR);
                    child_reaped = (reap_p == pid);
                }
                if (stdout_open) { close(stdout_pipe[0]); stdout_open = false; }
                if (stderr_open) { close(stderr_pipe[0]); stderr_open = false; }
                res->status = STATUS_RUNNER_INTERNAL_ERROR;
                break;
            }
        }
    }

    /*
     * Loop termination and tie-breaking:
     * Priority: OUTPUT_LIMIT_EXCEEDED > TIMEOUT > NORMAL_EXIT / SIGNAL_TERMINATION
     */
    pid_t wait_pid = 0;
    (void)wait_pid;

    if (output_cap_breached) {
        kill(-pid, SIGKILL);
        if (!child_reaped) {
            do {
                wait_pid = wait4(pid, &child_status, 0, &child_usage);
            } while (wait_pid == -1 && errno == EINTR);
        }
        if (stdout_open) drain_and_close_pipe(stdout_pipe[0], &res->stdout_buf, &res->stdout_len, &res->stdout_cap, cfg->max_output_bytes, &res->stdout_truncated);
        if (stderr_open) drain_and_close_pipe(stderr_pipe[0], &res->stderr_buf, &res->stderr_len, &res->stderr_cap, cfg->max_output_bytes, &res->stderr_truncated);
        res->status = STATUS_OUTPUT_LIMIT_EXCEEDED;
    } else if (timed_out) {
        kill(-pid, SIGKILL);
        if (!child_reaped) {
            do {
                wait_pid = wait4(pid, &child_status, 0, &child_usage);
            } while (wait_pid == -1 && errno == EINTR);
        }
        if (stdout_open) drain_and_close_pipe(stdout_pipe[0], &res->stdout_buf, &res->stdout_len, &res->stdout_cap, cfg->max_output_bytes, &res->stdout_truncated);
        if (stderr_open) drain_and_close_pipe(stderr_pipe[0], &res->stderr_buf, &res->stderr_len, &res->stderr_cap, cfg->max_output_bytes, &res->stderr_truncated);
        res->status = STATUS_TIMEOUT;
    } else if (child_reaped) {
        /* Best-effort cleanup of any surviving descendants holding pipes open */
        kill(-pid, SIGKILL);
        if (stdout_open) drain_and_close_pipe(stdout_pipe[0], &res->stdout_buf, &res->stdout_len, &res->stdout_cap, cfg->max_output_bytes, &res->stdout_truncated);
        if (stderr_open) drain_and_close_pipe(stderr_pipe[0], &res->stderr_buf, &res->stderr_len, &res->stderr_cap, cfg->max_output_bytes, &res->stderr_truncated);

        if (res->status == 0) {
            if (WIFEXITED(child_status)) {
                res->exit_code = WEXITSTATUS(child_status);
                if (res->exit_code == 0) {
                    res->status = STATUS_OK;
                } else {
                    res->status = STATUS_RUNTIME_ERROR;
                }
            } else if (WIFSIGNALED(child_status)) {
                res->status = STATUS_RUNTIME_ERROR;
                res->signal = WTERMSIG(child_status);
            } else {
                res->status = STATUS_INDETERMINATE_SETUP_STATUS;
            }
        }
    }

    if (stdin_open) {
        close(gate_pipe[1]);
        gate_pipe[1] = -1;
        stdin_open = false;
    }
    if (sigaction(SIGPIPE, &previous_sigpipe, NULL) < 0 && res->status == 0) {
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
    }

    if (artifact_status_pipe[0] >= 0) {
        unsigned char artifact_status = 0;
        ssize_t status_read;
        do {
            status_read = read(artifact_status_pipe[0], &artifact_status, sizeof(artifact_status));
        } while (status_read < 0 && errno == EINTR);
        close(artifact_status_pipe[0]);
        if (!timed_out && !output_cap_breached &&
            (status_read != (ssize_t)sizeof(artifact_status) ||
             (artifact_status == 2) || (artifact_status == 1 && res->status != STATUS_OK) ||
             (artifact_status == 0 && res->status == STATUS_OK))) {
            set_setup_failure(res, SETUP_STAGE_WORKSPACE, EIO);
        }
    }

    if (cleanup_job_resources(&job_cgroup, root_mountpoint) < 0) {
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    /* Compute elapsed wall time and CPU time */
    struct timespec end_tp;
    clock_gettime(CLOCK_MONOTONIC, &end_tp);
    uint64_t end_ms = (uint64_t)end_tp.tv_sec * 1000ULL + (uint64_t)end_tp.tv_nsec / 1000000ULL;
    res->wall_ms = (uint32_t)(end_ms - start_ms);

    res->cpu_ms = (uint32_t)(
        (uint64_t)child_usage.ru_utime.tv_sec * 1000ULL + (uint64_t)child_usage.ru_utime.tv_usec / 1000ULL +
        (uint64_t)child_usage.ru_stime.tv_sec * 1000ULL + (uint64_t)child_usage.ru_stime.tv_usec / 1000ULL
    );
    res->peak_memory_kb = (uint64_t)child_usage.ru_maxrss;

    return 0;
}

/*
 * JSON Escaping helper according to RFC 8259 (byte-oriented Latin-1 representation).
 *
 * Each byte 'c' of the raw binary stream is encoded as follows:
 * - Special JSON escapes:
 *     '"'  -> \"
 *     '\\' -> \\
 *     '\b' -> \b
 *     '\f' -> \f
 *     '\n' -> \n
 *     '\r' -> \r
 *     '\t' -> \t
 * - Control characters (< 0x20 or 0x7F) and all bytes >= 0x80:
 *     -> \u00XX (where XX is two-digit lowercase hex)
 * - Printable ASCII (0x20 <= c <= 0x7E, except '"' and '\\'):
 *     -> literal ASCII character 'c'
 *
 * This encoding is 100% pure ASCII, universally valid JSON across all decoders,
 * and maps every single byte 'c' directly to Unicode code point U+0000..U+00FF.
 *
 * Lossless Byte Reconstruction:
 * Any client decodes the JSON string into Unicode characters, then recovers the
 * exact original byte sequence via standard Latin-1 decoding:
 *   reconstructed = text.encode('latin1')
 * or equivalently:
 *   reconstructed = bytes(ord(c) for c in text)
 */
static void print_json_escaped_string(FILE *out, const char *data, size_t len) {
    if (!data) return;
    const unsigned char *p = (const unsigned char *)data;

    for (size_t i = 0; i < len; i++) {
        unsigned char c = p[i];
        if (c == '"') {
            fputs("\\\"", out);
        } else if (c == '\\') {
            fputs("\\\\", out);
        } else if (c == '\b') {
            fputs("\\b", out);
        } else if (c == '\f') {
            fputs("\\f", out);
        } else if (c == '\n') {
            fputs("\\n", out);
        } else if (c == '\r') {
            fputs("\\r", out);
        } else if (c == '\t') {
            fputs("\\t", out);
        } else if (c < 0x20 || c >= 0x7F) {
            fprintf(out, "\\u%04x", (unsigned int)c);
        } else {
            fputc(c, out);
        }
    }
}

static const char *sandbox_status_to_string(const sandbox_result_t *res, const sandbox_config_t *cfg) {
    switch (res->status) {
        case STATUS_OK:
            return "OK";
        case STATUS_TIMEOUT:
            return "TIMEOUT";
        case STATUS_OUTPUT_LIMIT_EXCEEDED:
            return "OUTPUT_LIMIT";
        case STATUS_RUNTIME_ERROR:
            if (res->signal == 24 /* SIGXCPU */) {
                return "TIMEOUT";
            }
            if (res->signal == 25 /* SIGXFSZ */) {
                return "OUTPUT_LIMIT";
            }
            if (res->signal == 11 /* SIGSEGV */) {
                /*
                 * Heuristic: In POSIX, setrlimit(RLIMIT_AS) lacks an explicit kernel OOM signal.
                 * When heap allocation (malloc/mmap) fails, programs typically dereference NULL,
                 * triggering SIGSEGV. We evaluate a resident-memory heuristic: if peak RSS reached
                 * >= 80% of rlimit_as_bytes, the runner classifies this as MEMORY_LIMIT.
                 * Note that this is a heuristic rather than causal proof; ordinary SIGSEGV without
                 * reaching the memory ceiling remains classified as RUNTIME_ERROR.
                 */
                if (cfg && cfg->rlimit_as_bytes > 0 &&
                    (res->peak_memory_kb * 1024ULL >= (cfg->rlimit_as_bytes * 8ULL / 10ULL))) {
                    return "MEMORY_LIMIT";
                }
                return "RUNTIME_ERROR";
            }
            return "RUNTIME_ERROR";
        case STATUS_SETUP_ERROR:
        case STATUS_SETUP_PROTOCOL_ERROR:
        case STATUS_INDETERMINATE_SETUP_STATUS:
        case STATUS_RUNNER_CHILD_LOST:
        case STATUS_RUNNER_INTERNAL_ERROR:
        default:
            return "SANDBOX_ERROR";
    }
}

void sandbox_result_print_json(FILE *out, const sandbox_result_t *res, const char *job_id, const sandbox_config_t *cfg) {
    if (!out || !res) return;

    const char *status_str = sandbox_status_to_string(res, cfg);
    const char *jid = job_id ? job_id : "";

    fprintf(out, "{\"job_id\":\"");
    print_json_escaped_string(out, jid, strlen(jid));
    fprintf(out, "\",\"status\":\"%s\"", status_str);
    fprintf(out, ",\"exit_code\":%d", res->exit_code);
    fprintf(out, ",\"signal\":%d", res->signal);
    fprintf(out, ",\"wall_ms\":%u", res->wall_ms);
    fprintf(out, ",\"cpu_ms\":%u", res->cpu_ms);
    fprintf(out, ",\"peak_memory_kb\":%llu", (unsigned long long)res->peak_memory_kb);

    fprintf(out, ",\"stdout\":\"");
    if (res->stdout_buf && res->stdout_len > 0) {
        print_json_escaped_string(out, res->stdout_buf, res->stdout_len);
    }
    fprintf(out, "\"");

    fprintf(out, ",\"stderr\":\"");
    if (res->status == STATUS_SETUP_ERROR) {
        char err_msg[256];
        snprintf(err_msg, sizeof(err_msg), "setup error: stage=%d errno=%d (%s)",
                 res->setup_err.stage, res->setup_err.errnum, strerror(res->setup_err.errnum));
        print_json_escaped_string(out, err_msg, strlen(err_msg));
    } else if (res->status == STATUS_SETUP_PROTOCOL_ERROR) {
        const char *err_msg = "setup protocol error";
        print_json_escaped_string(out, err_msg, strlen(err_msg));
    } else if (res->stderr_buf && res->stderr_len > 0) {
        print_json_escaped_string(out, res->stderr_buf, res->stderr_len);
    }
    fprintf(out, "\"");

    fprintf(out, "}\n");
}

int run_sandbox(const char *binary_path) {
    sandbox_config_t cfg;
    sandbox_config_init_default(&cfg);

    sandbox_result_t res;
    int ret = run_sandbox_with_config(binary_path, &cfg, &res);
    sandbox_result_print_json(stdout, &res, cfg.job_id, &cfg);
    sandbox_result_cleanup(&res);
    return ret;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s [options] <path-to-binary>\n", argv[0]);
        fprintf(stderr, "Options:\n");
        fprintf(stderr, "  --job-id <id>          Job identifier string\n");
        fprintf(stderr, "  --timeout-ms <ms>      Wall-clock timeout in milliseconds (default: 2000)\n");
        fprintf(stderr, "  --cpu-soft <s>         RLIMIT_CPU soft limit in seconds (default: 1)\n");
        fprintf(stderr, "  --cpu-hard <s>         RLIMIT_CPU hard limit in seconds (default: 2)\n");
        fprintf(stderr, "  --as-mb <mb>           RLIMIT_AS limit in MiB (default: 128)\n");
        fprintf(stderr, "  --fsize-mb <mb>        RLIMIT_FSIZE limit in MiB (default: 1)\n");
        fprintf(stderr, "  --max-output-kb <kb>   Output stream cap in KiB (default: 64)\n");
        fprintf(stderr, "  --workspace <dir>       Private /tmp/pes-job-* workspace\n");
        fprintf(stderr, "  --source-file <name>    Copy source.c or source.py into /workspace\n");
        fprintf(stderr, "  --artifact-out <name>   Copy /workspace/target back after success\n");
        fprintf(stderr, "  stdin                  Target stdin from standard input (max 64 KiB)\n");
        fprintf(stderr, "  -- <binary> [args...]  Target executable and arguments (max 128, 4096 bytes each)\n");
        return 1;
    }

    sandbox_config_t cfg;
    sandbox_config_init_default(&cfg);

    const char *binary_path = NULL;
    const char *workspace_path = NULL;
    const char *workspace_source_name = NULL;
    const char *artifact_name = NULL;
    bool target_args_started = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: -- requires a target binary\n");
                return 1;
            }
            binary_path = argv[++i];
            cfg.target_args = (const char *const *)&argv[i + 1];
            cfg.target_argc = (size_t)(argc - i - 1);
            target_args_started = true;
            break;
        } else if (strcmp(argv[i], "--job-id") == 0 && i + 1 < argc) {
            cfg.job_id = argv[++i];
        } else if (strcmp(argv[i], "--timeout-ms") == 0 && i + 1 < argc) {
            cfg.timeout_ms = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--cpu-soft") == 0 && i + 1 < argc) {
            cfg.rlimit_cpu_soft = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--cpu-hard") == 0 && i + 1 < argc) {
            cfg.rlimit_cpu_hard = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--as-mb") == 0 && i + 1 < argc) {
            cfg.rlimit_as_bytes = (uint64_t)strtoull(argv[++i], NULL, 10) * 1024ULL * 1024ULL;
        } else if (strcmp(argv[i], "--fsize-mb") == 0 && i + 1 < argc) {
            cfg.rlimit_fsize = (uint64_t)strtoull(argv[++i], NULL, 10) * 1024ULL * 1024ULL;
        } else if (strcmp(argv[i], "--nofile") == 0 && i + 1 < argc) {
            cfg.rlimit_nofile = (rlim_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--max-output-kb") == 0 && i + 1 < argc) {
            cfg.max_output_bytes = (size_t)strtoull(argv[++i], NULL, 10) * 1024ULL;
        } else if (strcmp(argv[i], "--workspace") == 0 && i + 1 < argc) {
            workspace_path = argv[++i];
        } else if (strcmp(argv[i], "--source-file") == 0 && i + 1 < argc) {
            workspace_source_name = argv[++i];
        } else if (strcmp(argv[i], "--artifact-out") == 0 && i + 1 < argc) {
            artifact_name = argv[++i];
        } else if (argv[i][0] != '-') {
            binary_path = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (!binary_path) {
        fprintf(stderr, "Error: no target binary specified\n");
        return 1;
    }
    char resolved_binary[PATH_MAX];
    if (realpath(binary_path, resolved_binary)) {
        binary_path = resolved_binary;
    }

    if (target_args_started && cfg.target_argc > MAX_TARGET_ARGS) {
        fprintf(stderr, "Error: target argument count exceeds %u\n", MAX_TARGET_ARGS);
        return 1;
    }

    if ((workspace_path == NULL) != (workspace_source_name == NULL) ||
        (artifact_name && !workspace_path)) {
        fprintf(stderr, "Error: --workspace and --source-file must be provided together; --artifact-out requires them\n");
        return 1;
    }
    char stdin_buffer[PES_MAX_STDIN_BYTES + 1U];
    size_t stdin_length = 0;
    if (!isatty(STDIN_FILENO)) {
        while (stdin_length <= PES_MAX_STDIN_BYTES) {
            ssize_t count = read(STDIN_FILENO, stdin_buffer + stdin_length,
                                 PES_MAX_STDIN_BYTES + 1U - stdin_length);
            if (count < 0 && errno == EINTR) continue;
            if (count < 0) {
                fprintf(stderr, "Error: failed to read runner stdin: %s\n", strerror(errno));
                return 1;
            }
            if (count == 0) break;
            stdin_length += (size_t)count;
        }
        if (stdin_length > PES_MAX_STDIN_BYTES) {
            fprintf(stderr, "Error: stdin exceeds maximum of %u bytes\n", PES_MAX_STDIN_BYTES);
            return 1;
        }
    }
    if (workspace_path) {
        if (phase2_workspace_open(workspace_path, workspace_source_name, artifact_name,
                geteuid(), 256U * 1024U, &cfg.workspace_source_fd, &cfg.artifact_output_fd) < 0) {
            fprintf(stderr, "Error: invalid or unsafe worker workspace: %s\n", strerror(errno));
            return 1;
        }
        cfg.workspace_source_name = workspace_source_name;
        cfg.workspace_artifact_name = artifact_name;
    }
    cfg.stdin_data = stdin_buffer;
    cfg.stdin_len = stdin_length;

    sandbox_result_t res;
    int ret = run_sandbox_with_config(binary_path, &cfg, &res);
    if (ret != 0) {
        res.status = STATUS_RUNNER_INTERNAL_ERROR;
    }

    sandbox_result_print_json(stdout, &res, cfg.job_id, &cfg);
    sandbox_result_cleanup(&res);
    if (cfg.workspace_source_fd >= 0) close(cfg.workspace_source_fd);
    if (cfg.artifact_output_fd >= 0) close(cfg.artifact_output_fd);
    return (ret == 0) ? 0 : 1;
}
