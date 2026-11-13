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

    pid_t pid = fork();
    if (pid < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    if (pid == 0) {
        /* Child process */
        close(err_pipe[0]); /* Close read end in child */

        char *const child_argv[] = { (char *)binary_path, NULL };
        char *const child_envp[] = { NULL };

        execve(binary_path, child_argv, child_envp);

        /* If execve returns, it failed */
        child_report_error_and_exit(err_pipe[1], SETUP_STAGE_EXECVE, errno);
    }

    /* Parent process */
    close(err_pipe[1]); /* Close write end in parent */

    /* Read error pipe */
    setup_error_t s_err;
    memset(&s_err, 0, sizeof(s_err));
    int err_res = parent_read_setup_error(err_pipe[0], &s_err);
    close(err_pipe[0]);

    /* Reap child process */
    int status = 0;
    struct rusage usage;
    pid_t wait_pid;
    do {
        wait_pid = wait4(pid, &status, 0, &usage);
    } while (wait_pid == -1 && errno == EINTR);

    if (wait_pid == -1) {
        res->status = STATUS_RUNNER_INTERNAL_ERROR;
        return -1;
    }

    /* Classify outcome */
    if (err_res == 1) {
        res->status = STATUS_SETUP_ERROR;
        res->setup_err = s_err;
        res->exit_code = 127;
        return 0;
    } else if (err_res == -1) {
        res->status = STATUS_SETUP_PROTOCOL_ERROR;
        res->exit_code = 127;
        return 0;
    } else {
        /* EOF on error pipe: evaluate wait status */
        if (WIFEXITED(status)) {
            res->status = STATUS_OK;
            res->exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            res->status = STATUS_RUNTIME_ERROR;
            res->signal = WTERMSIG(status);
        } else {
            res->status = STATUS_INDETERMINATE_SETUP_STATUS;
        }
        return 0;
    }
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
        sandbox_result_cleanup(&res);
        return 0;
    } else if (res.status == STATUS_SETUP_PROTOCOL_ERROR) {
        fprintf(stderr, "setup protocol error\n");
        sandbox_result_cleanup(&res);
        return 0;
    } else if (res.status == STATUS_OK) {
        printf("exit code %d\n", res.exit_code);
    } else if (res.status == STATUS_RUNTIME_ERROR) {
        printf("signal %d\n", res.signal);
    } else {
        fprintf(stderr, "status: %d\n", res.status);
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
