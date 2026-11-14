#include <unistd.h>
#include <stdlib.h>

int main(void) {
    /*
     * Phase 2 Adversarial Fixture: Fork Bomb
     * Attempt to repeatedly fork child processes to exhaust PID capacity.
     * In Phase 2, this must be safely contained by:
     *   1. cgroups v2 pids.max (enforcing a hard ceiling on concurrent tasks);
     *   2. PID namespaces (CLONE_NEWPID) ensuring all spawned processes are
     *      isolated from the host process table and reaped when PID 1 exits;
     *   3. Atomic cgroup cleanup via cgroup.kill.
     * DO NOT run against Phase 1 runner!
     */
    while (1) {
        (void)fork();
    }
    return 0;
}
