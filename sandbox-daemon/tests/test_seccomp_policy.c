#define _GNU_SOURCE

#include "../phase2_privilege.c"

#include <sys/socket.h>
#include <sys/wait.h>

int main(void) {
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0 || install_seccomp_allowlist() < 0) _exit(2);
        errno = 0;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0) {
            close(sock);
            _exit(3);
        }
        _exit(errno == EPERM ? 0 : 4);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return 5;
    puts("seccomp policy runtime probe: socket denied with EPERM under loaded filter");
    return 0;
}
