#include <stdio.h>

int main(void) {
    fprintf(stdout, "STDOUT_DATA\n");
    fflush(stdout);
    fprintf(stderr, "STDERR_DATA\n");
    fflush(stderr);
    return 0;
}
