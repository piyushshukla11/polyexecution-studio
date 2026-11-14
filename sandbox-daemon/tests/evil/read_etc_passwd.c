#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

/*
 * Phase 2 Adversarial Fixture: Host Filesystem Snooping Audit
 *
 * Checks access to:
 *   1. Host canary file: /etc/pes_host_canary
 *   2. System password database: /etc/passwd
 *
 * Distinguishes:
 *   - BREACH (exit 0): Host-only canary file was read, or host-specific user
 *     ("zogratis:") was observed in /etc/passwd. Note: In Phase 2, a container
 *     rootfs may legitimately provide its own minimal /etc/passwd (e.g. root/nobody),
 *     so reading a container-local file is not a breach unless host data leaks.
 *   - BLOCKED_BY_POLICY (exit 1): Access was rejected with expected container
 *     policy errnos: ENOENT (file missing in isolated rootfs), EACCES, or EPERM.
 *   - UNEXPECTED_FAILURE (exit 2): Operation failed with an unexpected errno
 *     or I/O error unrelated to isolation policy.
 *
 * DO NOT execute against Phase 1 runner!
 */

static void emit_diag(const char *op, const char *target, const char *status, int err) {
    char msg[256];
    int n;
    if (err != 0) {
        n = snprintf(msg, sizeof(msg),
                     "DIAG: OP=%s TARGET=%s STATUS=%s ERRNO=%d (%s)\n",
                     op, target, status, err, strerror(err));
    } else {
        n = snprintf(msg, sizeof(msg),
                     "DIAG: OP=%s TARGET=%s STATUS=%s\n",
                     op, target, status);
    }
    if (n > 0) {
        (void)write(STDERR_FILENO, msg, (size_t)n);
    }
}

int main(void) {
    /* Step 1: Check unambiguous host canary */
    int canary_fd = open("/etc/pes_host_canary", O_RDONLY);
    if (canary_fd >= 0) {
        char buf[64];
        ssize_t nr = read(canary_fd, buf, sizeof(buf) - 1);
        close(canary_fd);
        if (nr > 0) {
            buf[nr] = '\0';
            emit_diag("open", "/etc/pes_host_canary", "BREACH", 0);
            (void)write(STDOUT_FILENO, "HOST_CANARY_LEAKED\n", 19);
            return 0; /* Breach */
        }
    }

    /* Step 2: Check /etc/passwd */
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) {
        int err = errno;
        if (err == ENOENT || err == EACCES || err == EPERM) {
            emit_diag("open", "/etc/passwd", "BLOCKED_BY_POLICY", err);
            return 1; /* Expected policy block */
        }
        emit_diag("open", "/etc/passwd", "UNEXPECTED_FAILURE", err);
        return 2; /* Unrelated failure */
    }

    /* /etc/passwd was opened: check if it contains host-specific identity */
    char content[512];
    ssize_t n = read(fd, content, sizeof(content) - 1);
    close(fd);

    if (n < 0) {
        emit_diag("read", "/etc/passwd", "UNEXPECTED_FAILURE", errno);
        return 2;
    }

    content[n] = '\0';

    /*
     * Check if the file contains the host user ("zogratis:").
     * If the rootfs is isolated with a synthetic minimal /etc/passwd that
     * only contains container users (nobody, root) without host users,
     * it does not leak host identities.
     */
    if (strstr(content, "zogratis:") != NULL) {
        emit_diag("read", "/etc/passwd", "BREACH_HOST_USER_FOUND", 0);
        (void)write(STDOUT_FILENO, "HOST_PASSWD_LEAKED\n", 19);
        return 0; /* Breach: host user identity leaked */
    }

    /* Non-host minimal passwd file opened without host secrets */
    emit_diag("read", "/etc/passwd", "CONTAINER_LOCAL_PASSWD_OBSERVED", 0);
    return 1; /* Safe containment */
}
