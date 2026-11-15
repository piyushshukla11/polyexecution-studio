#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define main sandbox_runner_main
#include "../sandbox.c"
#undef main

static const char CANARY_TOKEN[] = "PES_CANARY_TOKEN_7b9a24_EXEC";

static int count_open_descriptors(void) {
    DIR *dir = opendir("/proc/self/fd");
    if (!dir) return -1;
    int count = 0;
    int dir_fd = dirfd(dir);
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        int fd = atoi(entry->d_name);
        if (fd == dir_fd) continue;
        count++;
    }
    closedir(dir);
    return count;
}

static int create_canary_script(char *script_path, size_t path_size) {
    if (snprintf(script_path, path_size, "/tmp/pes-canary-XXXXXX") >= (int)path_size) return -1;
    int fd = mkstemp(script_path);
    if (fd < 0) return -1;

    char script_body[512];
    snprintf(script_body, sizeof(script_body),
             "#!/bin/sh\n"
             "echo \"%s\"\n"
             "exit 0\n",
             CANARY_TOKEN);

    size_t len = strlen(script_body);
    if (write(fd, script_body, len) != (ssize_t)len) {
        close(fd);
        unlink(script_path);
        return -1;
    }
    close(fd);
    if (chmod(script_path, 0755) < 0) {
        unlink(script_path);
        return -1;
    }
    return 0;
}

static int verify_no_child_processes(void) {
    int wait_status = 0;
    errno = 0;
    pid_t orphan = waitpid(-1, &wait_status, WNOHANG);
    if (orphan > 0) {
        fprintf(stderr, "FAIL: orphaned child PID %ld detected (still waitable)\n", (long)orphan);
        return -1;
    }
    if (orphan == 0) {
        fprintf(stderr, "FAIL: child process is still running (waitpid returned 0)\n");
        return -1;
    }
    if (orphan == -1 && errno != ECHILD) {
        fprintf(stderr, "FAIL: unexpected waitpid error: %s (errno=%d)\n", strerror(errno), errno);
        return -1;
    }
    return 0;
}

static int test_cleanup_job_resources_decoupled(void) {
    char mountpoint[PATH_MAX];
    if (phase2_rootfs_create_mountpoint(mountpoint, sizeof(mountpoint)) < 0) {
        fprintf(stderr, "FAIL: phase2_rootfs_create_mountpoint failed\n");
        return 1;
    }
    if (access(mountpoint, F_OK) != 0) {
        fprintf(stderr, "FAIL: mountpoint was not created on disk\n");
        return 1;
    }

    /* Test with an inactive cgroup (active == false) */
    phase2_cgroup_t cgroup;
    memset(&cgroup, 0, sizeof(cgroup));
    cgroup.active = false;

    if (cleanup_job_resources(&cgroup, mountpoint) < 0) {
        fprintf(stderr, "FAIL: cleanup_job_resources returned error with inactive cgroup\n");
        return 1;
    }
    if (access(mountpoint, F_OK) == 0) {
        fprintf(stderr, "FAIL: mountpoint was not removed when cgroup was inactive\n");
        (void)rmdir(mountpoint);
        return 1;
    }

    /* Test with NULL cgroup pointer */
    if (phase2_rootfs_create_mountpoint(mountpoint, sizeof(mountpoint)) < 0) {
        fprintf(stderr, "FAIL: second phase2_rootfs_create_mountpoint failed\n");
        return 1;
    }
    if (cleanup_job_resources(NULL, mountpoint) < 0) {
        fprintf(stderr, "FAIL: cleanup_job_resources returned error with NULL cgroup\n");
        return 1;
    }
    if (access(mountpoint, F_OK) == 0) {
        fprintf(stderr, "FAIL: mountpoint was not removed when cgroup was NULL\n");
        (void)rmdir(mountpoint);
        return 1;
    }

    return 0;
}

static int test_fail_closed_runner_on_cgroup_failure(void) {
    char canary_script[PATH_MAX];
    if (create_canary_script(canary_script, sizeof(canary_script)) < 0) {
        fprintf(stderr, "FAIL: cannot create canary script\n");
        return 1;
    }

    int fds_before = count_open_descriptors();
    if (fds_before < 0) {
        fprintf(stderr, "FAIL: count_open_descriptors failed before runner invocation\n");
        unlink(canary_script);
        return 1;
    }

    sandbox_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 2000;
    cfg.rlimit_cpu_soft = 1;
    cfg.rlimit_cpu_hard = 2;
    cfg.rlimit_as_bytes = 64 * 1024 * 1024;
    cfg.rlimit_fsize = 1024 * 1024;
    cfg.rlimit_nofile = 64;
    cfg.max_output_bytes = 4096;

    sandbox_result_t res;
    memset(&res, 0, sizeof(res));

    /* Inject deterministic cgroup creation failure */
    pes_test_inject_cgroup_create_errno = ENOTSUP;
    int ret = run_sandbox_with_config(canary_script, &cfg, &res);
    pes_test_inject_cgroup_create_errno = 0;

    int fds_after = count_open_descriptors();
    if (fds_after < 0) {
        fprintf(stderr, "FAIL: count_open_descriptors failed after runner invocation\n");
        unlink(canary_script);
        return 1;
    }

    /* 1. Return code must be 0 (setup error handled cleanly, not crash) */
    if (ret != 0) {
        fprintf(stderr, "FAIL: run_sandbox_with_config returned %d, expected 0\n", ret);
        unlink(canary_script);
        return 1;
    }

    /* 2. Status must be STATUS_SETUP_ERROR */
    if (res.status != STATUS_SETUP_ERROR) {
        fprintf(stderr, "FAIL: res.status is %d, expected STATUS_SETUP_ERROR (%d)\n",
                res.status, STATUS_SETUP_ERROR);
        unlink(canary_script);
        return 1;
    }

    /* 3. Setup stage must identify cgroup creation failure */
    if (res.setup_err.stage != SETUP_STAGE_CGROUP_CREATE) {
        fprintf(stderr, "FAIL: res.setup_err.stage is %d, expected SETUP_STAGE_CGROUP_CREATE (%d)\n",
                res.setup_err.stage, SETUP_STAGE_CGROUP_CREATE);
        unlink(canary_script);
        return 1;
    }

    /* 4. Errno must report the injected ENOTSUP */
    if (res.setup_err.errnum != ENOTSUP) {
        fprintf(stderr, "FAIL: res.setup_err.errnum is %d, expected ENOTSUP (%d)\n",
                res.setup_err.errnum, ENOTSUP);
        unlink(canary_script);
        return 1;
    }

    /* 5. Standard setup exit code is 127 */
    if (res.exit_code != 127) {
        fprintf(stderr, "FAIL: res.exit_code is %d, expected 127\n", res.exit_code);
        unlink(canary_script);
        return 1;
    }

    /* 6. Observable signal: canary token must NOT be in stdout (target never executed) */
    if (res.stdout_len > 0 || (res.stdout_buf && strstr(res.stdout_buf, CANARY_TOKEN) != NULL)) {
        fprintf(stderr, "FAIL: canary token observed in stdout! Target was executed despite setup failure!\n");
        unlink(canary_script);
        return 1;
    }

    /* 7. Child process verification */
    if (verify_no_child_processes() < 0) {
        unlink(canary_script);
        return 1;
    }

    /* 8. Descriptor leak check */
    if (fds_before != fds_after) {
        fprintf(stderr, "FAIL: descriptor leak detected: before=%d, after=%d\n", fds_before, fds_after);
        unlink(canary_script);
        return 1;
    }

    unlink(canary_script);
    return 0;
}

static int test_fail_closed_runner_on_fork_failure(void) {
    char canary_script[PATH_MAX];
    if (create_canary_script(canary_script, sizeof(canary_script)) < 0) {
        fprintf(stderr, "FAIL: cannot create canary script for fork failure test\n");
        return 1;
    }

    int fds_before = count_open_descriptors();
    if (fds_before < 0) {
        fprintf(stderr, "FAIL: count_open_descriptors failed before fork failure test\n");
        unlink(canary_script);
        return 1;
    }

    sandbox_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.timeout_ms = 2000;
    cfg.rlimit_cpu_soft = 1;
    cfg.rlimit_cpu_hard = 2;
    cfg.rlimit_as_bytes = 64 * 1024 * 1024;
    cfg.rlimit_fsize = 1024 * 1024;
    cfg.rlimit_nofile = 64;
    cfg.max_output_bytes = 4096;

    sandbox_result_t res;
    memset(&res, 0, sizeof(res));

    /* Bypass cgroup creation failure so runner reaches fork(), then inject fork failure */
    pes_test_last_root_mountpoint[0] = '\0';
    pes_test_inject_cgroup_create_fake_success = true;
    pes_test_inject_fork_errno = ENOMEM;
    int ret = run_sandbox_with_config(canary_script, &cfg, &res);
    pes_test_inject_cgroup_create_fake_success = false;
    pes_test_inject_fork_errno = 0;

    int fds_after = count_open_descriptors();
    if (fds_after < 0) {
        fprintf(stderr, "FAIL: count_open_descriptors failed after fork failure test\n");
        unlink(canary_script);
        return 1;
    }

    /* 1. Return code must be 0 */
    if (ret != 0) {
        fprintf(stderr, "FAIL: run_sandbox_with_config returned %d on fork failure, expected 0\n", ret);
        unlink(canary_script);
        return 1;
    }

    /* 2. Status must be STATUS_SETUP_ERROR */
    if (res.status != STATUS_SETUP_ERROR) {
        fprintf(stderr, "FAIL: res.status is %d on fork failure, expected STATUS_SETUP_ERROR (%d)\n",
                res.status, STATUS_SETUP_ERROR);
        unlink(canary_script);
        return 1;
    }

    /* 3. Setup stage must report SETUP_STAGE_SUPERVISOR */
    if (res.setup_err.stage != SETUP_STAGE_SUPERVISOR) {
        fprintf(stderr, "FAIL: res.setup_err.stage is %d on fork failure, expected SETUP_STAGE_SUPERVISOR (%d)\n",
                res.setup_err.stage, SETUP_STAGE_SUPERVISOR);
        unlink(canary_script);
        return 1;
    }

    /* 4. Errno must report the injected ENOMEM */
    if (res.setup_err.errnum != ENOMEM) {
        fprintf(stderr, "FAIL: res.setup_err.errnum is %d, expected ENOMEM (%d)\n",
                res.setup_err.errnum, ENOMEM);
        unlink(canary_script);
        return 1;
    }

    /* 5. Target token must NOT be in stdout */
    if (res.stdout_len > 0 || (res.stdout_buf && strstr(res.stdout_buf, CANARY_TOKEN) != NULL)) {
        fprintf(stderr, "FAIL: canary token observed in stdout on fork failure!\n");
        unlink(canary_script);
        return 1;
    }

    /* 6. Directly verify that the allocated rootfs mountpoint was removed on disk */
    if (pes_test_last_root_mountpoint[0] == '\0') {
        fprintf(stderr, "FAIL: root mountpoint was not recorded during fork failure test\n");
        unlink(canary_script);
        return 1;
    }
    if (access(pes_test_last_root_mountpoint, F_OK) == 0) {
        fprintf(stderr, "FAIL: root mountpoint %s was NOT removed on fork failure!\n",
                pes_test_last_root_mountpoint);
        (void)rmdir(pes_test_last_root_mountpoint);
        unlink(canary_script);
        return 1;
    }
    if (errno != ENOENT) {
        fprintf(stderr, "FAIL: unexpected errno checking root mountpoint: %s (errno=%d)\n",
                strerror(errno), errno);
        unlink(canary_script);
        return 1;
    }

    /* 7. Child process verification */
    if (verify_no_child_processes() < 0) {
        unlink(canary_script);
        return 1;
    }

    /* 8. Descriptor leak check (specifically verifies userns_ready_pipe and setup pipes cleanup) */
    if (fds_before != fds_after) {
        fprintf(stderr, "FAIL: descriptor leak detected on fork failure: before=%d, after=%d\n",
                fds_before, fds_after);
        unlink(canary_script);
        return 1;
    }

    unlink(canary_script);
    return 0;
}

int main(void) {
    if (test_cleanup_job_resources_decoupled() != 0) return 1;
    if (test_fail_closed_runner_on_cgroup_failure() != 0) return 2;
    if (test_fail_closed_runner_on_fork_failure() != 0) return 3;
    puts("fail-closed gate regression: decoupled rootfs cleanup PASS, injected cgroup-failure token absence/zero FD leak PASS, injected fork-failure rootfs cleanup/pipe cleanup/zero FD leak PASS");
    return 0;
}
