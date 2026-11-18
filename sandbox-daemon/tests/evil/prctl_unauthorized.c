#include <sys/prctl.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    char name[16] = {0};
    /* 1. PR_SET_NAME and PR_GET_NAME must succeed */
    if (prctl(PR_SET_NAME, "valid-name", 0, 0, 0) < 0) {
        return 1;
    }
    if (prctl(PR_GET_NAME, (unsigned long)name, 0, 0, 0) < 0 || strcmp(name, "valid-name") != 0) {
        return 2;
    }

    /* 2. Unrelated prctl operation must be killed by seccomp with SIGSYS */
    prctl(PR_SET_DUMPABLE, 1, 0, 0, 0);

    /* If we reach here, seccomp failed to block the unauthorized operation */
    return 42;
}
