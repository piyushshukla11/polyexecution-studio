#include <unistd.h>

int main(void) {
    char buf[1024];
    for (int i = 0; i < 1024; i++) buf[i] = 'E';
    for (int i = 0; i < 4; i++) {
        (void)write(STDERR_FILENO, buf, sizeof(buf));
    }
    return 0;
}
