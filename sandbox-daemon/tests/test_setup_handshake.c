#define main sandbox_runner_main
#include "../sandbox.c"
#undef main

static uint64_t monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0) return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static int test_clean_eof(void) {
    int fds[2];
    setup_error_t record;
    if (pipe(fds) < 0) return 1;
    close(fds[1]);
    int result = parent_read_setup_error(fds[0], &record, monotonic_ms() + 1000);
    close(fds[0]);
    return result == 0 ? 0 : 1;
}

static int test_complete_record(void) {
    int fds[2];
    setup_error_t expected = {.stage = SETUP_STAGE_EXECVE, .errnum = ENOENT};
    setup_error_t actual = {0};
    if (pipe(fds) < 0) return 1;
    if (write(fds[1], &expected, sizeof(expected)) != (ssize_t)sizeof(expected)) return 1;
    close(fds[1]);
    int result = parent_read_setup_error(fds[0], &actual, monotonic_ms() + 1000);
    close(fds[0]);
    return result == 1 && actual.stage == expected.stage && actual.errnum == expected.errnum ? 0 : 1;
}

static int test_partial_record_is_protocol_error(void) {
    int fds[2];
    setup_error_t expected = {.stage = SETUP_STAGE_EXECVE, .errnum = ENOENT};
    setup_error_t actual = {0};
    if (pipe(fds) < 0) return 1;
    if (write(fds[1], &expected, sizeof(expected) / 2) != (ssize_t)(sizeof(expected) / 2)) return 1;
    close(fds[1]);
    int result = parent_read_setup_error(fds[0], &actual, monotonic_ms() + 1000);
    close(fds[0]);
    return result == -1 ? 0 : 1;
}

static int test_stalled_handshake_times_out(void) {
    int fds[2];
    setup_error_t actual = {0};
    if (pipe(fds) < 0) return 1;
    pid_t writer = fork();
    if (writer < 0) return 1;
    if (writer == 0) {
        close(fds[0]);
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 250000000};
        (void)nanosleep(&pause, NULL);
        close(fds[1]);
        _exit(0);
    }

    close(fds[1]);
    uint64_t start = monotonic_ms();
    int result = parent_read_setup_error(fds[0], &actual, start + 50);
    uint64_t elapsed = monotonic_ms() - start;
    close(fds[0]);
    int status = 0;
    struct rusage usage;
    memset(&usage, 0, sizeof(usage));
    if (result != -2 || terminate_and_reap_setup_child(writer, &status, &usage) != 0) return 1;
    if (result != -2 || elapsed > 200) return 1;
    return WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL ? 0 : 1;
}

static int test_unrelated_descriptor_is_closed(void) {
    int gate[2], stdout_pipe[2], stderr_pipe[2];
    if (pipe(gate) < 0 || pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) return 1;
    int original_unrelated = open("/dev/null", O_RDONLY | O_CLOEXEC);
    if (original_unrelated < 0) return 1;
    int unrelated = fcntl(original_unrelated, F_DUPFD_CLOEXEC, 256);
    if (close(original_unrelated) < 0) return 1;
    if (unrelated < 256) return 1;
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        close(gate[0]);
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        if (dup2(stdout_pipe[1], STDOUT_FILENO) < 0 || dup2(stderr_pipe[1], STDERR_FILENO) < 0) _exit(2);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
#ifdef PES_TEST_FORCE_CLOSE_RANGE_FAILURE
        struct rlimit descriptor_limit;
        if (getrlimit(RLIMIT_NOFILE, &descriptor_limit) < 0) _exit(4);
        descriptor_limit.rlim_cur = 128;
        if (setrlimit(RLIMIT_NOFILE, &descriptor_limit) < 0) _exit(5);
#endif
        int result = close_unrelated_fds(gate[1], -1, -1, -1);
        int inherited = fcntl(unrelated, F_GETFD);
        int preserved = fcntl(gate[1], F_GETFD);
        if (result != 0 || inherited >= 0 || errno != EBADF || preserved < 0) _exit(1);
        if (write(STDOUT_FILENO, "o", 1) != 1 || write(STDERR_FILENO, "e", 1) != 1) _exit(3);
        _exit(0);
    }
    close(gate[0]);
    close(gate[1]);
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    close(unrelated);
    char out = '\0', err = '\0';
    if (read(stdout_pipe[0], &out, 1) != 1 || read(stderr_pipe[0], &err, 1) != 1) return 1;
    close(stdout_pipe[0]);
    close(stderr_pipe[0]);
    int status = 0;
    if (waitpid(child, &status, 0) != child) return 1;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 && out == 'o' && err == 'e' ? 0 : 1;
}

int main(void) {
    if (test_clean_eof() != 0) return 1;
    if (test_complete_record() != 0) return 2;
    if (test_partial_record_is_protocol_error() != 0) return 3;
    if (test_stalled_handshake_times_out() != 0) return 4;
    if (test_unrelated_descriptor_is_closed() != 0) return 5;
#ifdef PES_TEST_FORCE_CLOSE_RANGE_FAILURE
    puts("setup protocol and FD inheritance: 5/5 passed (forced /proc fallback; high FD closed)");
#else
    puts("setup protocol and FD inheritance: 5/5 passed (including bounded stall timeout)");
#endif
    return 0;
}
