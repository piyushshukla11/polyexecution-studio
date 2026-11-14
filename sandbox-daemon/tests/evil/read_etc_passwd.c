#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Phase 2 Adversarial Fixture: Host Filesystem Snooping Audit
 *
 * Tests isolation against unauthorized access to host filesystem resources:
 *   1. Dynamic host canary file (unique path passed via argv[1] or PES_CANARY_PATH).
 *   2. System password database (/etc/passwd).
 *
 * Safe Test Contract for Future Containment Suites:
 *   - The future test runner generates a unique, unguessable host canary file
 *     (e.g. /tmp/pes_canary_<uuid>.txt) containing a random token.
 *   - The canary file is NOT included or bind-mounted into the sandbox's minimal rootfs.
 *   - The canary path is passed via argv[1] (or PES_CANARY_PATH), and expected token via argv[2].
 *   - The canary file is created immediately before test execution and removed immediately
 *     afterward in a test fixture cleanup block.
 *   - DO NOT create canary files during preparation passes!
 *
 * Distinguishes:
 *   - BREACH (exit 0): Host canary file was read and matched expected token,
 *     or host user ("zogratis:") was leaked from /etc/passwd.
 *   - BLOCKED_BY_POLICY (exit 1): Access was rejected with expected container
 *     policy errnos: ENOENT (file absent in private rootfs), EACCES, EPERM,
 *     or /etc/passwd was opened but contained only clean container-local accounts.
 *   - UNEXPECTED_FAILURE (exit 2): Operation failed with an unexpected errno
 *     or I/O error unrelated to containment policy.
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

int main(int argc, char *argv[]) {
    /* Resolve canary path: argv[1] -> env var -> fallback default */
    const char *canary_path = "/etc/pes_host_canary";
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        canary_path = argv[1];
    } else {
        const char *env_path = getenv("PES_CANARY_PATH");
        if (env_path != NULL && env_path[0] != '\0') {
            canary_path = env_path;
        }
    }

    /* Resolve expected canary token: argv[2] -> env var -> NULL */
    const char *canary_token = NULL;
    if (argc > 2 && argv[2] != NULL && argv[2][0] != '\0') {
        canary_token = argv[2];
    } else {
        canary_token = getenv("PES_CANARY_TOKEN");
    }

    /* Resolve host user pattern for /etc/passwd check */
    const char *host_user = getenv("PES_HOST_USER");
    if (host_user == NULL || host_user[0] == '\0') {
        host_user = "zogratis:";
    }

    /* --------------------------------------------------------------------- */
    /* Step 1: Check dynamic host canary file                                 */
    /* --------------------------------------------------------------------- */
    int canary_fd = open(canary_path, O_RDONLY);
    if (canary_fd >= 0) {
        char buf[256];
        ssize_t nr = read(canary_fd, buf, sizeof(buf) - 1);
        close(canary_fd);

        if (nr < 0) {
            emit_diag("read", canary_path, "UNEXPECTED_FAILURE", errno);
            return 2;
        }

        buf[nr] = '\0';
        /* If token was specified, verify it matches; otherwise any content is a breach */
        if (canary_token == NULL || strstr(buf, canary_token) != NULL) {
            emit_diag("open", canary_path, "BREACH_CANARY_READ", 0);
            (void)write(STDOUT_FILENO, "HOST_CANARY_LEAKED\n", 19);
            return 0; /* Breach: host canary file leaked */
        }
    } else {
        int err = errno;
        if (err == ENOENT || err == EACCES || err == EPERM) {
            emit_diag("open", canary_path, "BLOCKED_BY_POLICY", err);
        } else {
            emit_diag("open", canary_path, "UNEXPECTED_FAILURE", err);
            return 2;
        }
    }

    /* --------------------------------------------------------------------- */
    /* Step 2: Check /etc/passwd                                             */
    /* --------------------------------------------------------------------- */
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

    char content[512];
    ssize_t n = read(fd, content, sizeof(content) - 1);
    close(fd);

    if (n < 0) {
        emit_diag("read", "/etc/passwd", "UNEXPECTED_FAILURE", errno);
        return 2;
    }

    content[n] = '\0';

    /*
     * Check if /etc/passwd contains the host user identifier.
     * In an isolated rootfs, /etc/passwd may exist with minimal container accounts
     * (e.g. root, nobody). That is NOT a breach unless host data is present.
     */
    if (strstr(content, host_user) != NULL) {
        emit_diag("read", "/etc/passwd", "BREACH_HOST_USER_FOUND", 0);
        (void)write(STDOUT_FILENO, "HOST_PASSWD_LEAKED\n", 19);
        return 0; /* Breach: host user identity leaked */
    }

    /* Container-local minimal passwd file without host user data */
    emit_diag("read", "/etc/passwd", "CONTAINER_LOCAL_PASSWD_OBSERVED", 0);
    return 1; /* Safe containment */
}
