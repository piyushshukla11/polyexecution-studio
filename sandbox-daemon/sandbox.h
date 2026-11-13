#ifndef SANDBOX_H
#define SANDBOX_H

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>

/* Setup stages for error reporting between child and parent */
typedef enum {
    SETUP_STAGE_OK = 0,
    SETUP_STAGE_SETSID,
    SETUP_STAGE_DUP2_STDOUT,
    SETUP_STAGE_DUP2_STDERR,
    SETUP_STAGE_CLOSE_FDS,
    SETUP_STAGE_SETRLIMIT_CPU,
    SETUP_STAGE_SETRLIMIT_AS,
    SETUP_STAGE_SETRLIMIT_FSIZE,
    SETUP_STAGE_SETRLIMIT_CORE,
    SETUP_STAGE_SETRLIMIT_NOFILE,
    SETUP_STAGE_EXECVE
} setup_stage_t;

/* Binary record transmitted over err_pipe on failure */
typedef struct {
    int32_t stage;  /* setup_stage_t */
    int32_t errnum; /* errno */
} setup_error_t;

/* Execution status classifications */
typedef enum {
    STATUS_OK = 0,
    STATUS_RUNTIME_ERROR,
    STATUS_TIMEOUT,
    STATUS_OUTPUT_LIMIT_EXCEEDED,
    STATUS_SETUP_ERROR,
    STATUS_SETUP_PROTOCOL_ERROR,
    STATUS_INDETERMINATE_SETUP_STATUS,
    STATUS_RUNNER_CHILD_LOST,
    STATUS_RUNNER_INTERNAL_ERROR
} sandbox_status_t;

/* Configuration parameters */
typedef struct {
    const char *job_id;        /* optional job identifier */
    uint32_t timeout_ms;       /* e.g. 2000 ms wall timeout */
    uint32_t rlimit_cpu_soft;  /* e.g. 1 s */
    uint32_t rlimit_cpu_hard;  /* e.g. 2 s */
    uint64_t rlimit_as_bytes;  /* e.g. 128 MiB = 128 * 1024 * 1024 */
    uint64_t rlimit_fsize;     /* e.g. 1 MiB = 1024 * 1024 */
    uint32_t rlimit_nofile;    /* e.g. 64 */
    size_t   max_output_bytes; /* e.g. 64 KiB = 64 * 1024 */
} sandbox_config_t;

/* Execution results */
typedef struct {
    sandbox_status_t status;
    int exit_code;
    int signal;
    uint32_t wall_ms;
    uint32_t cpu_ms;
    uint64_t peak_memory_kb;
    char *stdout_buf;
    size_t stdout_len;
    size_t stdout_cap;
    bool stdout_truncated;
    char *stderr_buf;
    size_t stderr_len;
    size_t stderr_cap;
    bool stderr_truncated;
    setup_error_t setup_err;
} sandbox_result_t;

/* Default configuration helper */
void sandbox_config_init_default(sandbox_config_t *cfg);

/* Free heap buffers inside result */
void sandbox_result_cleanup(sandbox_result_t *res);

/* Run target binary with given configuration */
int run_sandbox_with_config(const char *binary_path, const sandbox_config_t *cfg, sandbox_result_t *res);

/* Emit JSON result matching ExecuteResponse schema in docs/02_ARCHITECTURE.md */
void sandbox_result_print_json(FILE *out, const sandbox_result_t *res, const char *job_id, const sandbox_config_t *cfg);

/* Backward-compatible runner */
int run_sandbox(const char *binary_path);

#endif /* SANDBOX_H */
