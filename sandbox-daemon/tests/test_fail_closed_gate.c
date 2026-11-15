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

static int test_fail_closed_runner_on_unsupported_host(void) {
    char canary_script[] = "/tmp/pes-canary-XXXXXX";
    int fd = mkstemp(canary_script);
    if (fd < 0) {
        fprintf(stderr, "FAIL: cannot create temporary canary script\n");
        return 1;
    }

    char marker_path[PATH_MAX];
    snprintf(marker_path, sizeof(marker_path), "%s.marker", canary_script);
    (void)unlink(marker_path);

    char script_body[PATH_MAX + 128];
    snprintf(script_body, sizeof(script_body), "#!/bin/sh\necho canary > \"%s\"\nexit 0\n", marker_path);
    if (write(fd, script_body, strlen(script_body)) != (ssize_t)strlen(script_body)) {
        close(fd);
        unlink(canary_script);
        return 1;
    }
    close(fd);
    if (chmod(canary_script, 0755) < 0) {
        unlink(canary_script);
        return 1;
    }

    int fds_before = count_open_descriptors();

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

    int ret = run_sandbox_with_config(canary_script, &cfg, &res);

    int fds_after = count_open_descriptors();

    /* 1. Return code must be 0 (setup error handled cleanly, not unhandled crash) */
    if (ret != 0) {
        fprintf(stderr, "FAIL: run_sandbox_with_config returned %d, expected 0\n", ret);
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    /* 2. Status must be STATUS_SETUP_ERROR */
    if (res.status != STATUS_SETUP_ERROR) {
        fprintf(stderr, "FAIL: res.status is %d, expected STATUS_SETUP_ERROR (%d)\n",
                res.status, STATUS_SETUP_ERROR);
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    /* 3. Setup stage must identify the exact cgroup gate failure */
    if (res.setup_err.stage != SETUP_STAGE_CGROUP_CREATE) {
        fprintf(stderr, "FAIL: res.setup_err.stage is %d, expected SETUP_STAGE_CGROUP_CREATE (%d)\n",
                res.setup_err.stage, SETUP_STAGE_CGROUP_CREATE);
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    /* 4. Errno must report ENOTSUP (missing required cpu controller in cgroup delegation) */
    if (res.setup_err.errnum != ENOTSUP) {
        fprintf(stderr, "FAIL: res.setup_err.errnum is %d, expected ENOTSUP (%d)\n",
                res.setup_err.errnum, ENOTSUP);
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    /* 5. Standard setup exit code is 127 */
    if (res.exit_code != 127) {
        fprintf(stderr, "FAIL: res.exit_code is %d, expected 127\n", res.exit_code);
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    /* 6. Canary marker file must NOT exist (target binary was NEVER executed) */
    if (access(marker_path, F_OK) == 0) {
        fprintf(stderr, "FAIL: canary marker file was created! Target was executed despite setup failure!\n");
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    /* 7. Zero zombie or child processes left un-reaped */
    int wait_status = 0;
    pid_t orphan = waitpid(-1, &wait_status, WNOHANG);
    if (orphan > 0) {
        fprintf(stderr, "FAIL: orphaned child PID %ld detected\n", (long)orphan);
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    /* 8. Zero descriptor leakage */
    if (fds_before > 0 && fds_after > 0 && fds_before != fds_after) {
        fprintf(stderr, "FAIL: descriptor leak detected: before=%d, after=%d\n", fds_before, fds_after);
        unlink(canary_script);
        unlink(marker_path);
        return 1;
    }

    unlink(canary_script);
    unlink(marker_path);
    return 0;
}

int main(void) {
    if (test_cleanup_job_resources_decoupled() != 0) return 1;
    if (test_fail_closed_runner_on_unsupported_host() != 0) return 2;
    puts("fail-closed gate regression: decoupled rootfs cleanup PASS, fail-closed target non-execution PASS, zero resource/FD leak PASS");
    return 0;
}
