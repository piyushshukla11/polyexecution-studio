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

int main(void) {
    if (test_clean_eof() != 0) return 1;
    if (test_complete_record() != 0) return 2;
    if (test_partial_record_is_protocol_error() != 0) return 3;
    if (test_stalled_handshake_times_out() != 0) return 4;
    puts("setup handshake protocol: 4/4 passed (including bounded stall timeout)");
    return 0;
}
