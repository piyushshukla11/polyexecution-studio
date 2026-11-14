#include <unistd.h>
#include <stdlib.h>

int main(void) {
    /*
     * Phase 2 Adversarial Fixture: Fork Bomb
     * Attempt to repeatedly fork child processes to exhaust PID capacity.
     * In Phase 2, this must be safely contained by PID namespaces (CLONE_NEWPID)
     * and cgroups v2 pids.max without impacting the host process table.
     * DO NOT run against Phase 1 runner!
     */
    while (1) {
        (void)fork();
    }
    return 0;
}
