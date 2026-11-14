#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>

/*
 * Phase 2 Adversarial Fixture: Network Abuse Diagnostic Audit
 *
 * Attempts TCP socket creation and connect to external address 93.184.216.34:80.
 *
 * Distinguishes:
 *   - BREACH (exit 0): Outbound TCP connection succeeded.
 *   - BLOCKED_BY_POLICY (exit 1):
 *       * socket() blocked with EPERM or EACCES (seccomp syscall policy)
 *       * connect() failed immediately with ENETUNREACH (network namespace has
 *         no routes and loopback is down)
 *   - UNEXPECTED_FAILURE (exit 2):
 *       * connect() failed with ambient routing/firewall errnos (ETIMEDOUT,
 *         EHOSTUNREACH, ECONNREFUSED), which represent environmental network
 *         issues rather than verified Linux namespace or seccomp isolation.
 *
 * DO NOT execute against Phase 1 runner!
 */

static void emit_diag(const char *op, const char *status, int err) {
    char msg[256];
    int n;
    if (err != 0) {
        n = snprintf(msg, sizeof(msg),
                     "DIAG: OP=%s TARGET=93.184.216.34:80 STATUS=%s ERRNO=%d (%s)\n",
                     op, status, err, strerror(err));
    } else {
        n = snprintf(msg, sizeof(msg),
                     "DIAG: OP=%s TARGET=93.184.216.34:80 STATUS=%s\n",
                     op, status);
    }
    if (n > 0) {
        (void)write(STDERR_FILENO, msg, (size_t)n);
    }
}

int main(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        int err = errno;
        if (err == EPERM || err == EACCES) {
            /* Seccomp syscall filter blocked socket creation */
            emit_diag("socket", "BLOCKED_BY_POLICY", err);
            return 1;
        }
        emit_diag("socket", "UNEXPECTED_FAILURE", err);
        return 2;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "93.184.216.34", &addr.sin_addr);

    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    int err = (ret < 0) ? errno : 0;
    close(sock);

    if (ret == 0) {
        /* Breach: Outbound connection established */
        emit_diag("connect", "BREACH", 0);
        (void)write(STDOUT_FILENO, "NETWORK_CONNECTED\n", 18);
        return 0;
    }

    /*
     * Disambiguate policy denial from ambient failure:
     * - In an isolated network namespace (CLONE_NEWNET), the routing table is empty.
     *   The kernel rejects connect() synchronously with ENETUNREACH (Network is unreachable).
     * - If seccomp blocks connect(), errno is EPERM or EACCES.
     * - ETIMEDOUT, EHOSTUNREACH, or ECONNREFUSED indicate ambient network drops or firewall
     *   filters, NOT proven sandbox namespace isolation.
     */
    if (err == ENETUNREACH || err == EPERM || err == EACCES) {
        emit_diag("connect", "BLOCKED_BY_POLICY", err);
        return 1;
    }

    /* Ambient network or routing failure */
    emit_diag("connect", "UNEXPECTED_FAILURE", err);
    return 2;
}
