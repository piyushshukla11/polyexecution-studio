#include <unistd.h>
#include <stdlib.h>

/*
 * Phase 2 Adversarial Fixture: Fork Bomb
 *
 * DANGEROUS: DO NOT EXECUTE!
 * Execution against the Phase 1 runner or uncontained host will exhaust
 * system PIDs, potentially causing denial of service to the user session.
 *
 * Phase 2 Containment Architecture:
 *   1. Cgroups v2 `pids.max` (e.g. 16 or 32) enforced under the runner's
 *      delegated user slice.
 *   2. PID namespace (`CLONE_NEWPID`) isolating the process tree from the host.
 *   3. Atomic cleanup via `cgroup.kill` writing 1 to terminate all descendants.
 *
 * Future Test Verification:
 *   - The future test runner will inspect `/sys/fs/cgroup/.../pids.events`
 *     to confirm `max > 0`, establishing direct causal proof that the Linux
 *     kernel enforced the task ceiling when fork() failed with EAGAIN.
 *   - Unrelated test failures (e.g. startup crashes) show `max == 0`.
 */

int main(void) {
    while (1) {
        (void)fork();
    }
    return 0;
}
