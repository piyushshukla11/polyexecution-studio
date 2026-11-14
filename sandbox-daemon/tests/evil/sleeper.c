#include <unistd.h>

int main(void) {
    /* Sleep indefinitely to test wall-clock timeout enforcement */
    sleep(9999);
    return 0;
}
