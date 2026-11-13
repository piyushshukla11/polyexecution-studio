#define _GNU_SOURCE

#include "sandbox.h"

/*
 * run_sandbox:
 * Executes a target binary in a child process and waits for it to finish.
 *
 * Process lifecycle:
 * 1. Validates input binary path.
 * 2. Forks child process (fork(2)).
 * 3. Child executes target binary using execve(2) with explicit path and minimal envp.
 *    On execve failure, writes error message to stderr via write(2) and calls _exit(127).
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
         * The child process executes the target binary.
         *
         * execve(2) replaces the current process image with a new process image.
         * It takes three arguments:
         * 1. filename: pathname of the binary to execute.
         * 2. argv: NULL-terminated array of argument strings passed to the new program.
         *    By convention, argv[0] contains the filename being executed.
         * 3. envp: NULL-terminated array of environment variables ("NAME=VALUE").
         *
         * Why execve instead of execvp / execlp?
         * - execvp searches the PATH environment variable and inherits all environment variables.
         * - execve requires an explicit pathname and caller-specified environment.
         * - In a sandbox, inheriting arbitrary environment variables (e.g. LD_PRELOAD, PATH)
         *   is a security risk. Passing a minimal fixed envp ({ NULL }) ensures determinism.
         */
        char *const child_argv[] = { (char *)binary_path, NULL };
        char *const child_envp[] = { NULL };

        execve(binary_path, child_argv, child_envp);

        /*
         * If execve() succeeds, it NEVER returns because the process image is replaced.
         * If execution reaches here, execve() failed (e.g. ENOENT file not found, EACCES permission denied).
         *
         * Error reporting in child:
         * We use write(2) directly to STDERR_FILENO rather than printf/perror because
         * write(2) is an async-signal-safe system call that does not allocate memory
         * or touch stdio locks/buffers.
         */
        const char err_msg[] = "execve failed\n";
        (void)write(STDERR_FILENO, err_msg, sizeof(err_msg) - 1);

        /*
         * Standard convention for command not found or exec failure: exit 127.
         * We call _exit(2) to terminate immediately without invoking parent handlers
         * or flushing parent stdio buffers.
         */
        _exit(127);
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
