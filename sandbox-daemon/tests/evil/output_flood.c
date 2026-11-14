#include <unistd.h>

int main(void) {
    char buf[1024];
    for (int i = 0; i < 1024; i++) buf[i] = 'A';
    /* Write 128 KiB to stdout to trigger 64 KiB output cap */
    for (int i = 0; i < 128; i++) {
        if (write(STDOUT_FILENO, buf, sizeof(buf)) < 0) {
            break;
        }
    }
    return 0;
}
