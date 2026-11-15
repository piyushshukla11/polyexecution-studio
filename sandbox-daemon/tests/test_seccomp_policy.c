#define _GNU_SOURCE

#include "../phase2_privilege.c"

#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>

int main(void) {
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) < 0 || install_seccomp_allowlist() < 0) _exit(2);
        errno = 0;
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock >= 0 || errno != EPERM) _exit(3);
        struct sockaddr_in address = {.sin_family = AF_INET, .sin_port = htons(9)};
        errno = 0;
        if (connect(-1, (struct sockaddr *)&address, sizeof(address)) == 0 || errno != EPERM) _exit(4);

        pid_t startup = fork();
        if (startup < 0) _exit(5);
        if (startup == 0) {
            char *const argv[] = {"/bin/true", NULL};
            char *const envp[] = {NULL};
            execve(argv[0], argv, envp);
            _exit(6);
        }
        int startup_status = 0;
        if (waitpid(startup, &startup_status, 0) != startup || !WIFEXITED(startup_status) ||
            WEXITSTATUS(startup_status) != 0) _exit(7);
        _exit(0);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return 5;
    puts("seccomp policy runtime probe: /bin/true startup passed; socket and connect denied with EPERM");
    return 0;
}
