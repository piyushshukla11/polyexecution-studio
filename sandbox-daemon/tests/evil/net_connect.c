#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(void) {
    /*
     * Phase 2 Adversarial Fixture: Network Abuse
     * Attempt to create an outbound TCP socket and connect to an external host.
     * In Phase 2, this must be blocked by:
     *   1. Network namespaces (CLONE_NEWNET) where loopback is down and no external
     *      route exists, causing connect() to fail immediately with ENETUNREACH;
     *   2. Seccomp-BPF filtering blocking the socket() or connect() syscalls.
     * DO NOT run against Phase 1 runner!
     */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        /* Blocked at socket creation by seccomp policy */
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "93.184.216.34", &addr.sin_addr);

    int ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    close(sock);

    if (ret == 0) {
        /* Breach: Outbound network connection succeeded */
        write(STDOUT_FILENO, "NETWORK_CONNECTED\n", 18);
        return 0;
    }

    /* Safe Phase 2 containment: connect failed (e.g. ENETUNREACH) */
    return 2;
}
