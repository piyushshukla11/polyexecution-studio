#define _GNU_SOURCE

#include "../phase2_userns.h"

#include <errno.h>
#include <grp.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
}

static int child_wait_release(int fd) {
    unsigned char value = 0;
    ssize_t n;
    do { n = read(fd, &value, 1); } while (n < 0 && errno == EINTR);
    return n == 1 && value == 1 ? 0 : -1;
}

int main(void) {
    int ready_pipe[2], release_pipe[2];
    if (pipe(ready_pipe) < 0 || pipe(release_pipe) < 0) return 1;
    pid_t child = fork();
    if (child < 0) return 2;
    if (child == 0) {
        close(ready_pipe[0]);
        close(release_pipe[1]);
        if (phase2_userns_child_start(ready_pipe[1]) < 0) _exit(20);
        close(ready_pipe[1]);
        if (child_wait_release(release_pipe[0]) < 0) _exit(21);
        close(release_pipe[0]);
        if (setgroups(0, NULL) < 0 || getgroups(0, NULL) != 0) _exit(22);
        if (setgid(65534) < 0 || setuid(65534) < 0) _exit(23);
        if (getuid() != 65534 || geteuid() != 65534 || getgid() != 65534 || getegid() != 65534) _exit(24);
        _exit(0);
    }

    close(ready_pipe[1]);
    close(release_pipe[0]);
    struct pollfd pfd = {.fd = ready_pipe[0], .events = POLLIN | POLLHUP, .revents = 0};
    int poll_result = poll(&pfd, 1, 1000);
    phase2_userns_ready_t ready = {0};
    ssize_t received = poll_result > 0 ? read(ready_pipe[0], &ready, sizeof(ready)) : -1;
    close(ready_pipe[0]);
    if (received != (ssize_t)sizeof(ready) || ready.result != 0) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        fprintf(stderr, "safe userns mapping probe skipped: unshare failed (errno=%d)\n", ready.errnum);
        return 77;
    }

    bool gid_map_failed = false;
    if (phase2_userns_parent_map(child, monotonic_ms() + 2000, &gid_map_failed) < 0) {
        int map_errno = errno;
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        fprintf(stderr, "safe userns mapping probe failed: %s map error errno=%d\n",
                gid_map_failed ? "GID" : "UID", map_errno);
        return 3;
    }

    unsigned char release = 1;
    if (write(release_pipe[1], &release, 1) != 1) {
        (void)kill(child, SIGKILL);
        (void)waitpid(child, NULL, 0);
        close(release_pipe[1]);
        return 4;
    }
    close(release_pipe[1]);
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "safe userns mapping probe: child verification failed status=%d\n", status);
        return 5;
    }
    puts("safe userns runtime probe passed: UID/GID maps verified, setgroups cleared, identity dropped");
    return 0;
}
