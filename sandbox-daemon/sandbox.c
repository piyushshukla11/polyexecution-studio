#define _GNU_SOURCE

#include "sandbox.h"

int run_sandbox(const char *binary_path) {
    if (binary_path == NULL) {
        fprintf(stderr, "Error: binary_path is NULL\n");
        return -1;
    }

    // TODO(piyush): 1. Fork a child process.
    // Call fork() to spawn the child process.
    // Check if fork() failed (pid < 0). Handle error.
    // Differentiate between parent (pid > 0) and child (pid == 0).

    // --- Child process logic ---
    // TODO(piyush): 2. Execute target binary using execve.
    // Construct argv array with binary_path and NULL terminator.
    // Construct safe/clean envp array (e.g., NULL or minimal environment).
    // Call execve(binary_path, argv, envp).
    // If execve() returns, it failed. Call perror() and _exit(127).

    // --- Parent process logic ---
    // TODO(piyush): 3. Wait for child process termination using wait4.
    // Declare int status and struct rusage usage.
    // Call wait4(child_pid, &status, 0, &usage). Check for error (-1).

    // TODO(piyush): 4. Decode and print exit status or terminating signal.
    // Check if child exited normally with WIFEXITED(status) and get code with WEXITSTATUS(status).
    // Check if child was terminated by signal with WIFSIGNALED(status) and get signal with WTERMSIG(status).

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
