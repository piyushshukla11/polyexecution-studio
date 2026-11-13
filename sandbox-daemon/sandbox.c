#define _GNU_SOURCE

#include "sandbox.h"

void sandbox_config_init_default(sandbox_config_t *cfg) {
    if (!cfg) return;
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
 * Bounded non-blocking pipe drain helper.
 * Reads available bytes from pipe_fd into *buf up to max_bytes.
 * Once kernel pipe buffer is empty (EAGAIN/EWOULDBLOCK) or EOF is reached, closes pipe_fd.
 * Never blocks waiting for uncooperative descendants.
 */
static void drain_and_close_pipe(int pipe_fd, char **buf, size_t *len, size_t *cap, size_t max_bytes, bool *truncated) {
    if (pipe_fd < 0) return;
    char sink[4096];

    while (true) {
        size_t space = (max_bytes > *len) ? (max_bytes - *len) : 0;
        if (space > 0) {
            if (*len + 4096 > *cap) {
                size_t new_cap = (*cap == 0) ? 4096 : (*cap * 2);
                if (new_cap > max_bytes) new_cap = max_bytes;
                char *new_buf = realloc(*buf, new_cap);
                if (!new_buf) break;
                *buf = new_buf;
                *cap = new_cap;
            }
            size_t to_read = *cap - *len;
            if (to_read > 4096) to_read = 4096;
            ssize_t n = read(pipe_fd, *buf + *len, to_read);
            if (n > 0) {
                *len += (size_t)n;
            } else if (n == 0) {
                break; /* EOF */
            } else {
                if (errno == EINTR) continue;
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                break; /* Read error */
            }
        } else {
            /* Cap reached: read non-blocking into sink to drain kernel buffer without growing memory */
            *truncated = true;
            ssize_t n = read(pipe_fd, sink, sizeof(sink));
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
        (void)wait4(pid, &child_status, 0, &child_usage);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        res->status = STATUS_SETUP_ERROR;
        res->setup_err = s_err;
        res->exit_code = 127;
        return 0;
    } else if (err_res == -1) {
        /* Protocol error: child closed pipe mid-record or write failed */
        int child_status = 0;
        struct rusage child_usage;
        (void)wait4(pid, &child_status, 0, &child_usage);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);

        res->status = STATUS_SETUP_PROTOCOL_ERROR;
        res->exit_code = 127;
        return 0;
    }

    /*
     * err_res == 0 (EOF): No setup error record arrived.
     * Enter poll-based event loop with continuous direct-child liveness checking.
     */
    size_t stdout_cap = 0, stderr_cap = 0;
    bool child_reaped = false;
    int child_status = 0;
    struct rusage child_usage;
    pid_t wait_pid = 0;
    (void)wait_pid;
    memset(&child_usage, 0, sizeof(child_usage));

    bool timed_out = false;
    bool output_cap_breached = false;
    bool stdout_open = true;
    bool stderr_open = true;

    while (!child_reaped) {
        struct timespec now_tp;
        clock_gettime(CLOCK_MONOTONIC, &now_tp);
        uint64_t now_ms = (uint64_t)now_tp.tv_sec * 1000ULL + (uint64_t)now_tp.tv_nsec / 1000000ULL;

        if (now_ms >= deadline_ms) {
            timed_out = true;
            break;
        }

        uint64_t remaining_ms = deadline_ms - now_ms;
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
                /* Fatal poll error */
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
                                res->stdout_buf = realloc(res->stdout_buf, res->stdout_len + keep);
                                memcpy(res->stdout_buf + res->stdout_len, chunk, keep);
                                res->stdout_len += keep;
                            }
                            res->stdout_truncated = true;
                            output_cap_breached = true;
                            break;
                        } else {
                            res->stdout_buf = realloc(res->stdout_buf, res->stdout_len + (size_t)n);
                            memcpy(res->stdout_buf + res->stdout_len, chunk, (size_t)n);
                            res->stdout_len += (size_t)n;
                            stdout_cap = res->stdout_len;
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
                if (output_cap_breached) break;
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
                                res->stderr_buf = realloc(res->stderr_buf, res->stderr_len + keep);
                                memcpy(res->stderr_buf + res->stderr_len, chunk, keep);
                                res->stderr_len += keep;
                            }
                            res->stderr_truncated = true;
                            output_cap_breached = true;
                            break;
                        } else {
                            res->stderr_buf = realloc(res->stderr_buf, res->stderr_len + (size_t)n);
                            memcpy(res->stderr_buf + res->stderr_len, chunk, (size_t)n);
                            res->stderr_len += (size_t)n;
                            stderr_cap = res->stderr_len;
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
                if (output_cap_breached) break;
            }
        } else {
            /* Both pipes closed: sleep briefly while respecting deadline */
            (void)poll(NULL, 0, poll_timeout);
        }

        /* Check direct-child status via non-blocking wait4 */
        pid_t reap_pid = wait4(pid, &child_status, WNOHANG, &child_usage);
        if (reap_pid == pid) {
            child_reaped = true;
            break;
        } else if (reap_pid == 0) {
            /* Child still running, continue loop */
        } else if (reap_pid == -1) {
            if (errno == EINTR) {
                continue;
            } else if (errno == ECHILD) {
                /* Child already reaped or not waitable */
                child_reaped = true;
                res->status = STATUS_RUNNER_CHILD_LOST;
                break;
            } else {
                /* Fatal wait4 error */
                res->status = STATUS_RUNNER_INTERNAL_ERROR;
                break;
            }
        }
    }

    /*
     * Loop termination and tie-breaking:
     * Priority: OUTPUT_LIMIT_EXCEEDED > TIMEOUT > NORMAL_EXIT / SIGNAL_TERMINATION
     */
    if (output_cap_breached) {
        kill(-pid, SIGKILL);
        if (!child_reaped) {
            do {
                wait_pid = wait4(pid, &child_status, 0, &child_usage);
            } while (wait_pid == -1 && errno == EINTR);
        }
        if (stdout_open) drain_and_close_pipe(stdout_pipe[0], &res->stdout_buf, &res->stdout_len, &stdout_cap, cfg->max_output_bytes, &res->stdout_truncated);
        if (stderr_open) drain_and_close_pipe(stderr_pipe[0], &res->stderr_buf, &res->stderr_len, &stderr_cap, cfg->max_output_bytes, &res->stderr_truncated);
        res->status = STATUS_OUTPUT_LIMIT_EXCEEDED;
    } else if (timed_out) {
        kill(-pid, SIGKILL);
        if (!child_reaped) {
            do {
                wait_pid = wait4(pid, &child_status, 0, &child_usage);
            } while (wait_pid == -1 && errno == EINTR);
        }
        if (stdout_open) drain_and_close_pipe(stdout_pipe[0], &res->stdout_buf, &res->stdout_len, &stdout_cap, cfg->max_output_bytes, &res->stdout_truncated);
        if (stderr_open) drain_and_close_pipe(stderr_pipe[0], &res->stderr_buf, &res->stderr_len, &stderr_cap, cfg->max_output_bytes, &res->stderr_truncated);
        res->status = STATUS_TIMEOUT;
    } else if (child_reaped) {
        /* Best-effort cleanup of any surviving descendants holding pipes open */
        kill(-pid, SIGKILL);
        if (stdout_open) drain_and_close_pipe(stdout_pipe[0], &res->stdout_buf, &res->stdout_len, &stdout_cap, cfg->max_output_bytes, &res->stdout_truncated);
        if (stderr_open) drain_and_close_pipe(stderr_pipe[0], &res->stderr_buf, &res->stderr_len, &stderr_cap, cfg->max_output_bytes, &res->stderr_truncated);

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

int run_sandbox(const char *binary_path) {
    sandbox_config_t cfg;
    sandbox_config_init_default(&cfg);

    sandbox_result_t res;
    int ret = run_sandbox_with_config(binary_path, &cfg, &res);
    if (ret != 0) {
        fprintf(stderr, "runner internal error\n");
        return ret;
    }

    if (res.status == STATUS_SETUP_ERROR) {
        fprintf(stderr, "setup error: stage=%d errno=%d (%s)\n",
                res.setup_err.stage, res.setup_err.errnum, strerror(res.setup_err.errnum));
    } else if (res.status == STATUS_SETUP_PROTOCOL_ERROR) {
        fprintf(stderr, "setup protocol error\n");
    } else if (res.status == STATUS_TIMEOUT) {
        printf("status: TIMEOUT\n");
    } else if (res.status == STATUS_OUTPUT_LIMIT_EXCEEDED) {
        printf("status: OUTPUT_LIMIT_EXCEEDED\n");
    } else if (res.status == STATUS_OK) {
        printf("exit code %d\n", res.exit_code);
    } else if (res.status == STATUS_RUNTIME_ERROR) {
        printf("signal %d\n", res.signal);
    } else {
        printf("status: %d\n", res.status);
    }

    sandbox_result_cleanup(&res);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-to-binary>\n", argv[0]);
        return 1;
    }

    const char *binary_path = argv[1];
    return run_sandbox(binary_path);
}
