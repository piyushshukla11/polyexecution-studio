#include <unistd.h>
#include <stdlib.h>

int main(void) {
    pid_t pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        /* Grandchild inherits stdout/stderr pipes and sleeps */
        sleep(60);
        _exit(0);
    }
    /* Direct child exits immediately with code 42 */
    return 42;
}
