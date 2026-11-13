#define _GNU_SOURCE

#include "sandbox.h"

void sandbox_config_init_default(sandbox_config_t *cfg) {
    if (!cfg) return;
    cfg->job_id = "";
    cfg->timeout_ms = 2000;
    cfg->rlimit_cpu_soft = 1;
    cfg->rlimit_cpu_hard = 2;
    cfg->rlimit_as_bytes = 128ULL * 1024ULL * 1024ULL; /* 128 MiB */
    cfg->rlimit_fsize = 1024ULL * 1024ULL;              /* 1 MiB */
    cfg->rlimit_nofile = 64;
    cfg->max_output_bytes = 64ULL * 1024ULL;            /* 64 KiB */
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

/*
 * Parent error reader:
 * Returns:
 *  1: Complete record read (sizeof(setup_error_t)) -> setup error reported.
 *  0: Clean EOF encountered with 0 bytes read -> no setup error record arrived.
 * -1: Partial record read or read error -> protocol error.
 */
static int parent_read_setup_error(int err_pipe_read_fd, setup_error_t *out_err) {
    size_t total = 0;
    char *buf = (char *)out_err;

    while (total < sizeof(setup_error_t)) {
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

int run_sandbox_with_config(const char *binary_path, const sandbox_config_t *cfg, sandbox_result_t *res) {
    if (!binary_path || !cfg || !res) {
        return -1;
    }
    memset(res, 0, sizeof(*res));

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

    /* Sample start monotonic time */
    struct timespec start_tp;
    clock_gettime(CLOCK_MONOTONIC, &start_tp);
    uint64_t start_ms = (uint64_t)start_tp.tv_sec * 1000ULL + (uint64_t)start_tp.tv_nsec / 1000000ULL;
    uint64_t deadline_ms = start_ms + (uint64_t)cfg->timeout_ms;

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]); close(err_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        close(stderr_pipe[0]); close(stderr_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    if (pid == 0) {
        /*
         * --- CHILD PROCESS ---
         */
        close(err_pipe[0]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

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

        /* 4. Execute target binary with scrubbed environment */
        char *const child_argv[] = { (char *)binary_path, NULL };
        char *const child_envp[] = { NULL };

        execve(binary_path, child_argv, child_envp);

        /* If execve returns, it failed */
        child_report_error_and_exit(err_pipe[1], SETUP_STAGE_EXECVE, errno);
    }

    /*
     * --- PARENT PROCESS ---
     */
    close(err_pipe[1]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);

    /* Read error pipe */
    setup_error_t s_err;
    memset(&s_err, 0, sizeof(s_err));
    int err_res = parent_read_setup_error(err_pipe[0], &s_err);
    close(err_pipe[0]);

    if (err_res == 1) {
        /* Child explicitly reported setup/exec failure */
        int child_status = 0;
        struct rusage child_usage;
        pid_t reap_pid;
        do {
            reap_pid = wait4(pid, &child_status, 0, &child_usage);
        } while (reap_pid == -1 && errno == EINTR);

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        if (reap_pid != pid) {
            res->status = STATUS_RUNNER_CHILD_LOST;
            return -1;
        }

        res->status = STATUS_SETUP_ERROR;
        res->setup_err = s_err;
        res->exit_code = 127;
        return 0;
    } else if (err_res == -1) {
        /* Protocol error: child closed pipe mid-record or write failed */
        int child_status = 0;
        struct rusage child_usage;
        pid_t reap_pid;
        do {
            reap_pid = wait4(pid, &child_status, 0, &child_usage);
        } while (reap_pid == -1 && errno == EINTR);

        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        if (reap_pid != pid) {
            res->status = STATUS_RUNNER_CHILD_LOST;
            return -1;
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

        if (stdout_open || stderr_open) {
            struct pollfd pfds[2];
            int nfds = 0;
            int stdout_idx = -1, stderr_idx = -1;

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
                /* Fatal wait4 error */
                kill(-pid, SIGKILL);
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
                res->status = STATUS_OK;
                res->exit_code = WEXITSTATUS(child_status);
            } else if (WIFSIGNALED(child_status)) {
                res->status = STATUS_RUNTIME_ERROR;
                res->signal = WTERMSIG(child_status);
            } else {
                res->status = STATUS_INDETERMINATE_SETUP_STATUS;
            }
        }
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
 * JSON Escaping helper according to RFC 8259.
 * Validates multi-byte UTF-8 sequences and preserves them directly.
 * Escapes quotes, backslashes, standard control characters, and invalid raw bytes as \u00XX.
 */
static void print_json_escaped_string(FILE *out, const char *data, size_t len) {
    if (!data) return;
    const unsigned char *p = (const unsigned char *)data;
    size_t i = 0;

    while (i < len) {
        unsigned char c = p[i];
        if (c == '"') {
            fputs("\\\"", out);
            i++;
        } else if (c == '\\') {
            fputs("\\\\", out);
            i++;
        } else if (c == '\b') {
            fputs("\\b", out);
            i++;
        } else if (c == '\f') {
            fputs("\\f", out);
            i++;
        } else if (c == '\n') {
            fputs("\\n", out);
            i++;
        } else if (c == '\r') {
            fputs("\\r", out);
            i++;
        } else if (c == '\t') {
            fputs("\\t", out);
            i++;
        } else if (c < 0x20 || c == 0x7F) {
            fprintf(out, "\\u%04x", c);
            i++;
        } else if (c < 0x80) {
            fputc(c, out);
            i++;
        } else {
            /* Check for valid UTF-8 sequence */
            bool valid = false;
            size_t seq_len = 0;

            if ((c & 0xE0) == 0xC0 && c >= 0xC2 && i + 1 < len) {
                if ((p[i + 1] & 0xC0) == 0x80) {
                    valid = true;
                    seq_len = 2;
                }
            } else if ((c & 0xF0) == 0xE0 && i + 2 < len) {
                unsigned char b1 = p[i + 1], b2 = p[i + 2];
                if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
                    if (!(c == 0xE0 && b1 < 0xA0) && !(c == 0xED && b1 > 0x9F)) {
                        valid = true;
                        seq_len = 3;
                    }
                }
            } else if ((c & 0xF8) == 0xF0 && i + 3 < len) {
                unsigned char b1 = p[i + 1], b2 = p[i + 2], b3 = p[i + 3];
                if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
                    if (!(c == 0xF0 && b1 < 0x90) && !(c == 0xF4 && b1 > 0x8F)) {
                        valid = true;
                        seq_len = 4;
                    }
                }
            }

            if (valid) {
                for (size_t s = 0; s < seq_len; s++) {
                    fputc(p[i + s], out);
                }
                i += seq_len;
            } else {
                /* Invalid byte: escape as \u00XX */
                fprintf(out, "\\u%04x", c);
                i++;
            }
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
                /* Truthful memory limit check: only map to MEMORY_LIMIT if peak memory reached >= 80% of RLIMIT_AS */
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
        return 1;
    }

    sandbox_config_t cfg;
    sandbox_config_init_default(&cfg);

    const char *binary_path = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--job-id") == 0 && i + 1 < argc) {
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
        } else if (strcmp(argv[i], "--max-output-kb") == 0 && i + 1 < argc) {
            cfg.max_output_bytes = (size_t)strtoull(argv[++i], NULL, 10) * 1024ULL;
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

    sandbox_result_t res;
    int ret = run_sandbox_with_config(binary_path, &cfg, &res);
    if (ret != 0) {
        res.status = STATUS_RUNNER_INTERNAL_ERROR;
    }

    sandbox_result_print_json(stdout, &res, cfg.job_id, &cfg);
    sandbox_result_cleanup(&res);
    return (ret == 0) ? 0 : 1;
}
