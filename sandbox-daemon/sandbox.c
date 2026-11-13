#define _GNU_SOURCE

#include "sandbox.h"

/*
 * run_sandbox:
 * Executes a target binary in a child process and waits for it to finish.
 *
 * Step 1 (a) milestone:
 * 1. Validates input binary path.
 * 2. Forks child process (fork(2)).
 * 3. Child exits with _exit(3) as a baseline lifecycle test.
 * 4. Parent reaps child with wait4(2), handling EINTR signal interruption.
 * 5. Decodes and prints child termination status.
 */
int run_sandbox(const char *binary_path) {
    if (binary_path == NULL) {
        fprintf(stderr, "Error: binary_path is NULL\n");
        return -1;
    }

    /*
     * fork(2) creates a new process by duplicating the calling process.
     * The child process and parent process run in separate memory spaces.
     *
     * Return value:
     * - In parent: returns the process ID (PID > 0) of the newly created child.
     * - In child:  returns 0.
     * - On failure: returns -1 in parent; no child process is created, errno set.
     */
    pid_t pid = fork();

    if (pid < 0) {
        /*
         * fork() failed (e.g. system PID limit or resource exhaustion).
         * We must NOT call wait4() because no child process was created.
         */
        perror("fork failed");
        return -1;
    }

    if (pid == 0) {
        /*
         * --- CHILD PROCESS (pid == 0) ---
         * For Step 1(a) milestone test: immediately exit with code 3.
         *
         * We use _exit(2) instead of exit(3) because:
         * 1. _exit terminates the process immediately via the kernel.
         * 2. It avoids flushing or corrupting stdio buffers inherited from parent.
         * 3. It skips running atexit(3) cleanup handlers registered in parent.
         */
        _exit(3);
    }

    /*
     * --- PARENT PROCESS (pid > 0) ---
     * The parent process reaches this point while the child executes.
     *
     * wait4(2) suspends execution of the calling process until a child has
     * changed state (here, terminated). It reaps the terminated process so it
     * does not linger as a zombie, and collects resource usage statistics.
     *
     * EINTR Handling:
     * If wait4() is interrupted by a caught signal before the child changes
     * state, wait4() returns -1 with errno set to EINTR. We loop and retry
     * wait4() until it succeeds or encounters a fatal error.
     */
    int status = 0;
    struct rusage usage;
    pid_t wait_pid;

    do {
        wait_pid = wait4(pid, &status, 0, &usage);
    } while (wait_pid == -1 && errno == EINTR);

    if (wait_pid == -1) {
        perror("wait4 failed");
        return -1;
    }

    /*
     * Status decoding:
     * The raw integer returned in 'status' encodes multiple bitfields:
     * whether the process exited normally, whether it was terminated by a signal,
     * whether a core dump was produced, etc.
     * Never print or interpret raw status directly; always use POSIX macros.
     */
    if (WIFEXITED(status)) {
        /* Child terminated normally via exit(), _exit(), or return from main */
        int exit_code = WEXITSTATUS(status);
        printf("exit code %d\n", exit_code);
    } else if (WIFSIGNALED(status)) {
        /* Child terminated abnormally due to an unhandled signal */
        int sig = WTERMSIG(status);
        printf("signal %d\n", sig);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <path-to-binary>\n", argv[0]);
        return 1;
    }

    const char *binary_path = argv[1];
    return run_sandbox(binary_path);
}
