#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Phase 2 Adversarial Fixture: Network Abuse Diagnostic Audit
 *
 * Tests isolation against unauthorized outbound network socket communication.
 *
 * Safe Test Contract for Future Containment Suites:
 *   - The future test runner configures a local test TCP listener (e.g. 127.0.0.1:port
 *     or a host veth pair) outside the sandbox, and passes the target IP via argv[1]
 *     (or PES_NET_TARGET_IP) and target port via argv[2] (or PES_NET_TARGET_PORT).
 *   - Hardcoding external public IPs (e.g. 93.184.216.34) is provided only as a fallback
 *     and must NOT be used for automated CI because air-gapped or offline hosts would
 *     fail connections due to lack of internet, producing false isolation claims.
 *   - DO NOT make live network calls during readiness preparation passes!
 *
 * Distinguishes:
 *   - BREACH (exit 0): Outbound TCP connection succeeded.
 *   - BLOCKED_BY_POLICY (exit 1):
 *       * socket() blocked with EPERM or EACCES (seccomp syscall allowlist policy).
 *       * connect() failed immediately with ENETUNREACH (empty network namespace
 *         with no routing interfaces).
 *       * connect() blocked with EPERM or EACCES (seccomp connect filter).
 *   - UNEXPECTED_FAILURE (exit 2):
 *       * connect() failed with ECONNREFUSED (CRITICAL: connection reached the host/port,
 *         proving network was REACHABLE; not isolated!).
 *       * connect() failed with ETIMEDOUT (ambient drop/firewall; not proven isolation).
 *       * connect() failed with EHOSTUNREACH, EADDRNOTAVAIL, or other routing error.
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
    /* Resolve target IP: argv[1] -> env var -> default documentation IP */
    const char *target_ip = "93.184.216.34";
    if (argc > 1 && argv[1] != NULL && argv[1][0] != '\0') {
        target_ip = argv[1];
    } else {
        const char *env_ip = getenv("PES_NET_TARGET_IP");
        if (env_ip != NULL && env_ip[0] != '\0') {
            target_ip = env_ip;
        }
    }

    /* Resolve target port: argv[2] -> env var -> 80 */
    int target_port = 80;
    if (argc > 2 && argv[2] != NULL && argv[2][0] != '\0') {
        target_port = atoi(argv[2]);
    } else {
        const char *env_port = getenv("PES_NET_TARGET_PORT");
        if (env_port != NULL && env_port[0] != '\0') {
            target_port = atoi(env_port);
        }
    }
    if (target_port <= 0 || target_port > 65535) {
        target_port = 80;
    }

    char target_str[64];
    snprintf(target_str, sizeof(target_str), "%s:%d", target_ip, target_port);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        int err = errno;
        if (err == EPERM || err == EACCES) {
            /* Seccomp syscall filter blocked socket creation */
            emit_diag("socket", target_str, "BLOCKED_BY_POLICY", err);
            return 1;
        }
        emit_diag("socket", target_str, "UNEXPECTED_FAILURE", err);
        return 2;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)target_port);
    if (inet_pton(AF_INET, target_ip, &addr.sin_addr) <= 0) {
        emit_diag("inet_pton", target_str, "UNEXPECTED_FAILURE", errno);
        close(sock);
        return 2;
    }

    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    int err = (ret < 0) ? errno : 0;
    close(sock);

    if (ret == 0) {
        /* Breach: Outbound connection established */
        emit_diag("connect", target_str, "BREACH", 0);
        (void)write(STDOUT_FILENO, "NETWORK_CONNECTED\n", 18);
        return 0;
    }

    /*
     * Disambiguate policy denial from ambient failure:
     * - In an isolated network namespace (CLONE_NEWNET), the routing table is empty.
     *   The kernel rejects connect() synchronously with ENETUNREACH (Network is unreachable).
     * - If seccomp blocks connect(), errno is EPERM or EACCES.
     * - ECONNREFUSED means packets reached the host/port (network was REACHABLE; not isolated!).
     * - ETIMEDOUT or EHOSTUNREACH indicate ambient drops or gateway issues, NOT proven isolation.
     */
    if (err == ENETUNREACH || err == EPERM || err == EACCES) {
        emit_diag("connect", target_str, "BLOCKED_BY_POLICY", err);
        return 1;
    }

    /* Ambient network or routing failure (including ECONNREFUSED) */
    emit_diag("connect", target_str, "UNEXPECTED_FAILURE", err);
    return 2;
}
