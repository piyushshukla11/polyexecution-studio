#define _GNU_SOURCE

#include "../phase2_cgroup.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *const fake_control_files[] = {
    "cpu.max", "memory.max", "pids.max", "cgroup.procs", "cgroup.kill", "cgroup.events"
};

static int write_text(const char *path, const char *value) {
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    int result = fputs(value, file) < 0 ? -1 : 0;
    if (fclose(file) < 0) result = -1;
    return result;
}

static int create_fake_control_files(const char *directory) {
    char path[4096];
    for (size_t i = 0; i < sizeof(fake_control_files) / sizeof(fake_control_files[0]); i++) {
        if (snprintf(path, sizeof(path), "%s/%s", directory, fake_control_files[i]) >= (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        const char *initial = strcmp(fake_control_files[i], "cgroup.events") == 0 ? "populated 0\n" : "";
        if (write_text(path, initial) < 0) return -1;
    }
    return 0;
}

/* The helper's mkdir call is redirected only in this test translation unit. */
static int test_mkdir(const char *path, mode_t mode) {
    if (syscall(SYS_mkdir, path, mode) < 0) return -1;
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    if (strncmp(leaf, "pes-", 4) == 0 && create_fake_control_files(path) < 0) return -1;
    return 0;
}

#define mkdir test_mkdir
#include "../phase2_cgroup.c"
#undef mkdir

static int fixture_path_join(char *path, size_t size, const char *base, const char *leaf) {
    int n = snprintf(path, size, "%s/%s", base, leaf);
    if (n < 0 || (size_t)n >= size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int read_text(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    size_t n = fread(buffer, 1, size - 1, file);
    int result = ferror(file) ? -1 : 0;
    if (fclose(file) < 0) result = -1;
    buffer[n] = '\0';
    return result;
}

static int expect_text(const char *directory, const char *name, const char *expected) {
    char path[4096];
    char actual[256];
    if (fixture_path_join(path, sizeof(path), directory, name) < 0 ||
        read_text(path, actual, sizeof(actual)) < 0) return -1;
    if (strcmp(actual, expected) != 0) {
        fprintf(stderr, "%s: expected [%s], got [%s]\n", name, expected, actual);
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int remove_fake_control_files(const char *directory) {
    char path[4096];
    for (size_t i = 0; i < sizeof(fake_control_files) / sizeof(fake_control_files[0]); i++) {
        if (fixture_path_join(path, sizeof(path), directory, fake_control_files[i]) < 0 || unlink(path) < 0) return -1;
    }
    return 0;
}

static int has_job_directory(const char *parent) {
    DIR *directory = opendir(parent);
    if (!directory) return -1;
    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "pes-", 4) == 0) found = 1;
    }
    if (closedir(directory) < 0) return -1;
    return found;
}

int main(void) {
    char root_template[] = "/tmp/pes-cgroup-test-XXXXXX";
    char *root = mkdtemp(root_template);
    if (!root) return 1;

    char parent[4096];
    char proc_file[4096];
    char path[4096];
    int result = 0;
    int parent_created = 0;
    int proc_created = 0;
    int job_created = 0;
    phase2_cgroup_t cgroup = {0};
    const char *parent_files[] = {"cgroup.controllers", "cgroup.subtree_control"};

    if (fixture_path_join(parent, sizeof(parent), root, "delegated") < 0 || mkdir(parent, 0700) < 0) {
        result = 2;
        goto cleanup;
    }
    parent_created = 1;
    if (fixture_path_join(path, sizeof(path), parent, "cgroup.controllers") < 0 ||
        write_text(path, "cpu memory pids\n") < 0 ||
        fixture_path_join(path, sizeof(path), parent, "cgroup.subtree_control") < 0 ||
        write_text(path, "cpu memory pids\n") < 0 ||
        fixture_path_join(proc_file, sizeof(proc_file), root, "proc-cgroup") < 0 ||
        write_text(proc_file, "0::/delegated\n") < 0) {
        result = 3;
        goto cleanup;
    }
    proc_created = 1;

    phase2_cgroup_limits_t limits = {
        .memory_max_bytes = 67108864ULL,
        .cpu_quota_us = 50000ULL,
        .cpu_period_us = 100000ULL,
        .pids_max = 17
    };
    errno = 0;
    if (phase2_cgroup_create(&cgroup, root, proc_file, &limits) < 0) {
        perror("create cgroup in valid fake tree");
        result = 4;
        goto cleanup;
    }
    job_created = 1;
    size_t parent_length = strlen(parent);
    if (strncmp(cgroup.path, parent, parent_length) != 0 || cgroup.path[parent_length] != '/' ||
        strncmp(cgroup.path + parent_length + 1, "pes-", 4) != 0 ||
        expect_text(cgroup.path, "cpu.max", "50000 100000") < 0 ||
        expect_text(cgroup.path, "memory.max", "67108864") < 0 ||
        expect_text(cgroup.path, "pids.max", "17") < 0) {
        result = 5;
        goto cleanup;
    }

    pid_t child = fork();
    if (child < 0) {
        result = 6;
        goto cleanup;
    }
    if (child == 0) _exit(0);
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child || !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 ||
        phase2_cgroup_attach(&cgroup, child) < 0) {
        result = 7;
        goto cleanup;
    }
    char expected_pid[32];
    snprintf(expected_pid, sizeof(expected_pid), "%ld", (long)child);
    if (expect_text(cgroup.path, "cgroup.procs", expected_pid) < 0 || phase2_cgroup_kill(&cgroup) < 0 ||
        expect_text(cgroup.path, "cgroup.kill", "1") < 0 ||
        expect_text(cgroup.path, "cgroup.events", "populated 0\n") < 0) {
        result = 8;
        goto cleanup;
    }
    if (remove_fake_control_files(cgroup.path) < 0 || phase2_cgroup_remove(&cgroup) < 0 || cgroup.active) {
        result = 9;
        goto cleanup;
    }
    job_created = 0;
    puts("fake-tree logic only: valid path, controllers, exact limits, child PID attach, cgroup.kill, populated=0, cleanup: PASS");

    if (fixture_path_join(path, sizeof(path), parent, "cgroup.subtree_control") < 0 ||
        write_text(path, "memory pids\n") < 0) {
        result = 10;
        goto cleanup;
    }
    errno = 0;
    if (phase2_cgroup_create(&cgroup, root, proc_file, &limits) == 0 || errno != ENOTSUP || has_job_directory(parent) != 0) {
        result = 11;
        goto cleanup;
    }
    puts("fake-tree logic only: missing required controller rejected before job creation: PASS");

    if (fixture_path_join(path, sizeof(path), parent, "cgroup.subtree_control") < 0 ||
        write_text(path, "cpu memory pids\n") < 0 ||
        fixture_path_join(path, sizeof(path), parent, "cgroup.controllers") < 0 ||
        write_text(path, "memory pids\n") < 0) {
        result = 22;
        goto cleanup;
    }
    errno = 0;
    if (phase2_cgroup_create(&cgroup, root, proc_file, &limits) == 0 || errno != ENOTSUP || has_job_directory(parent) != 0) {
        result = 23;
        goto cleanup;
    }
    puts("fake-tree logic only: unavailable required controller rejected before job creation: PASS");

    if (fixture_path_join(path, sizeof(path), parent, "cgroup.controllers") < 0 ||
        write_text(path, "cpu memory pids\n") < 0 ||
        fixture_path_join(path, sizeof(path), parent, "cgroup.subtree_control") < 0 ||
        write_text(path, "cpu memory pids\n") < 0 ||
        write_text(proc_file, "0::/delegated/../escape\n") < 0) {
        result = 12;
        goto cleanup;
    }
    errno = 0;
    if (phase2_cgroup_create(&cgroup, root, proc_file, &limits) == 0 || errno != EINVAL || has_job_directory(parent) != 0) {
        result = 13;
        goto cleanup;
    }
    puts("fake-tree logic only: malformed cgroup path rejected before job creation: PASS");

    if (write_text(proc_file, "0::/delegated\n") < 0) {
        result = 14;
        goto cleanup;
    }
    limits.pids_max = 0;
    errno = 0;
    if (phase2_cgroup_create(&cgroup, root, proc_file, &limits) == 0 || errno != EINVAL || has_job_directory(parent) != 0) {
        result = 15;
        goto cleanup;
    }
    puts("fake-tree logic only: invalid limit rejected before job creation: PASS");

cleanup:
    if (job_created) {
        if (remove_fake_control_files(cgroup.path) < 0) result = result ? result : 16;
        if (phase2_cgroup_remove(&cgroup) < 0) result = result ? result : 17;
    }
    if (proc_created && unlink(proc_file) < 0) result = result ? result : 18;
    if (parent_created) {
        for (size_t i = 0; i < sizeof(parent_files) / sizeof(parent_files[0]); i++) {
            if (fixture_path_join(path, sizeof(path), parent, parent_files[i]) < 0 || unlink(path) < 0) result = result ? result : 19;
        }
        if (rmdir(parent) < 0) result = result ? result : 20;
    }
    if (rmdir(root) < 0) result = result ? result : 21;
    return result;
}
