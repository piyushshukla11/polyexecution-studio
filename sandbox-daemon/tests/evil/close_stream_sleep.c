#include <unistd.h>

int main(void) {
    /* Close standard descriptors to simulate detached streams */
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    /* Sleep indefinitely to test persistent wall timeout */
    sleep(9999);
    return 0;
}
