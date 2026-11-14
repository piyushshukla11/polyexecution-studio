#define _GNU_SOURCE

#include "../phase2_cgroup.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int write_text(const char *path, const char *value) {
    FILE *file = fopen(path, "w");
    if (!file) return -1;
    int result = fputs(value, file) < 0 ? -1 : 0;
    if (fclose(file) < 0) result = -1;
    return result;
}

int main(void) {
    char root_template[] = "/tmp/pes-cgroup-test-XXXXXX";
    char *root = mkdtemp(root_template);
    if (!root) return 1;

    char parent[4096];
    char path[4096];
    if (snprintf(parent, sizeof(parent), "%s/delegated", root) >= (int)sizeof(parent) ||
        mkdir(parent, 0700) < 0) return 2;
    if (snprintf(path, sizeof(path), "%s/cgroup.controllers", parent) >= (int)sizeof(path) ||
        write_text(path, "cpu memory pids\n") < 0) return 3;
    if (snprintf(path, sizeof(path), "%s/cgroup.subtree_control", parent) >= (int)sizeof(path) ||
        write_text(path, "memory pids\n") < 0) return 4;
    char proc_file[4096];
    if (snprintf(proc_file, sizeof(proc_file), "%s/proc-cgroup", root) >= (int)sizeof(proc_file) ||
        write_text(proc_file, "0::/delegated\n") < 0) return 5;

    phase2_cgroup_limits_t limits;
    phase2_cgroup_limits_init(&limits);
    phase2_cgroup_t cgroup;
    errno = 0;
    if (phase2_cgroup_create(&cgroup, root, proc_file, &limits) == 0 || errno != ENOTSUP) return 6;

    DIR *directory = opendir(parent);
    if (!directory) return 7;
    struct dirent *entry;
    int found_job_dir = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "pes-", 4) == 0) found_job_dir = 1;
    }
    closedir(directory);
    if (found_job_dir) return 8;

    if (snprintf(path, sizeof(path), "%s/cgroup.controllers", parent) >= (int)sizeof(path)) return 9;
    unlink(path);
    if (snprintf(path, sizeof(path), "%s/cgroup.subtree_control", parent) >= (int)sizeof(path)) return 10;
    unlink(path);
    unlink(proc_file);
    rmdir(parent);
    rmdir(root);
    puts("cgroup delegation gate: unavailable controller rejected before cgroup creation");
    return 0;
}
