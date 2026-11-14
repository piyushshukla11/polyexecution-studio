#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(void) {
    /*
     * Phase 2 Adversarial Fixture: Network Abuse
     * Attempt to create an outbound TCP connection.
     * In Phase 2, this must be blocked by network namespaces (CLONE_NEWNET)
     * and/or seccomp socket syscall filtering.
     * DO NOT run against Phase 1 runner!
     */
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        /* Blocked at socket creation */
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

    /* Safe Phase 2 termination: connect failed */
    return 2;
}
