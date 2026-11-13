#include <stdlib.h>
#include <string.h>

int main(void) {
    /* Allocate chunks exceeding 128 MiB RLIMIT_AS */
    for (int i = 0; i < 256; i++) {
        char *p = malloc(1024 * 1024);
        if (!p) {
            /* Trigger SIGSEGV on NULL pointer dereference */
            *p = 1;
            return 1;
        }
        memset(p, 0x42, 1024 * 1024);
    }
    return 0;
}
