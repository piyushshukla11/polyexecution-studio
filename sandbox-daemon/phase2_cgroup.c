#define _GNU_SOURCE

#include "phase2_cgroup.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int path_join(char *dst, size_t dst_size, const char *base, const char *leaf) {
    int n = snprintf(dst, dst_size, "%s/%s", base, leaf);
    if (n < 0 || (size_t)n >= dst_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int read_file(const char *path, char *buf, size_t size) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    ssize_t n;
    do {
        n = read(fd, buf, size - 1);
    } while (n < 0 && errno == EINTR);
    int saved_errno = errno;
    if (close(fd) < 0 && n >= 0) return -1;
    if (n < 0) {
        errno = saved_errno;
        return -1;
    }
    buf[n] = '\0';
    return 0;
}

static bool has_word(const char *list, const char *word) {
    size_t word_len = strlen(word);
    const char *p = list;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') p++;
        const char *start = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n') p++;
        if ((size_t)(p - start) == word_len && memcmp(start, word, word_len) == 0) return true;
    }
    return false;
}

static int write_value(const char *directory, const char *name, const char *value) {
    char path[PATH_MAX];
    if (path_join(path, sizeof(path), directory, name) < 0) return -1;
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t length = strlen(value);
    size_t written = 0;
    while (written < length) {
        ssize_t n = write(fd, value + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            int saved_errno = n < 0 ? errno : EIO;
            if (close(fd) < 0 && saved_errno == 0) saved_errno = errno;
            errno = saved_errno;
            return -1;
        }
        written += (size_t)n;
    }
    if (close(fd) < 0) return -1;
    return 0;
}

static int get_current_cgroup(const char *proc_file, char *relative, size_t relative_size) {
    FILE *file = fopen(proc_file, "re");
    if (!file) return -1;
    char *line = NULL;
    size_t capacity = 0;
    int result = -1;
    while (getline(&line, &capacity, file) >= 0) {
        if (strncmp(line, "0::", 3) != 0) continue;
        char *path = line + 3;
        path[strcspn(path, "\r\n")] = '\0';
        if (path[0] != '/' || strstr(path, "..") != NULL ||
            snprintf(relative, relative_size, "%s", path) >= (int)relative_size) {
            errno = EINVAL;
            break;
        }
        result = 0;
        break;
    }
    int saved_errno = errno;
    free(line);
    if (fclose(file) < 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result < 0) errno = saved_errno ? saved_errno : ENOENT;
    return result;
}

static bool has_required_controllers(const char *controllers, const char *subtree) {
    static const char *const required[] = {"cpu", "memory", "pids"};
    for (size_t i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        if (!has_word(controllers, required[i]) || !has_word(subtree, required[i])) return false;
    }
    return true;
}

/*
 * Domain controllers may only be distributed by an empty non-root cgroup.
 * A desktop terminal scope is commonly populated and may not have cpu
 * delegated, even while an empty ancestor service cgroup is delegated.
 */
static int is_delegated_parent(const char *directory) {
    char controllers_path[PATH_MAX];
    char subtree_path[PATH_MAX];
    char processes_path[PATH_MAX];
    char controllers[1024];
    char subtree[1024];
    char processes[2];
    if (path_join(controllers_path, sizeof(controllers_path), directory, "cgroup.controllers") < 0 ||
        path_join(subtree_path, sizeof(subtree_path), directory, "cgroup.subtree_control") < 0 ||
        path_join(processes_path, sizeof(processes_path), directory, "cgroup.procs") < 0) return 0;
    if (read_file(controllers_path, controllers, sizeof(controllers)) < 0 ||
        read_file(subtree_path, subtree, sizeof(subtree)) < 0 ||
        read_file(processes_path, processes, sizeof(processes)) < 0) return 0;
    if (!has_required_controllers(controllers, subtree) || processes[0] != '\0') return 0;
    if (access(directory, W_OK | X_OK) < 0) return 0;
    return 1;
}

static int find_delegated_parent(const char *cgroup_root, const char *relative,
                                 char *parent_out, size_t parent_out_size) {
    char candidate[PATH_MAX];
    int n = snprintf(candidate, sizeof(candidate), "%s%s", cgroup_root, relative);
    if (n < 0 || (size_t)n >= sizeof(candidate)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    size_t root_length = strlen(cgroup_root);
    while (strncmp(candidate, cgroup_root, root_length) == 0 &&
           (candidate[root_length] == '/' || candidate[root_length] == '\0')) {
        if (is_delegated_parent(candidate)) {
            n = snprintf(parent_out, parent_out_size, "%s", candidate);
            if (n < 0 || (size_t)n >= parent_out_size) {
                errno = ENAMETOOLONG;
                return -1;
            }
            return 0;
        }
        if (strcmp(candidate, cgroup_root) == 0) break;
        char *separator = strrchr(candidate, '/');
        if (!separator || (size_t)(separator - candidate) < root_length) break;
        *separator = '\0';
    }
    errno = ENOTSUP;
    return -1;
}

void phase2_cgroup_limits_init(phase2_cgroup_limits_t *limits) {
    if (!limits) return;
    limits->memory_max_bytes = 128ULL * 1024ULL * 1024ULL;
    limits->cpu_quota_us = 100000ULL;
    limits->cpu_period_us = 100000ULL;
    limits->pids_max = 32;
}

int phase2_cgroup_create(phase2_cgroup_t *cgroup,
                         const char *cgroup_root,
                         const char *proc_cgroup_file,
                         const phase2_cgroup_limits_t *limits) {
    if (!cgroup || !cgroup_root || !proc_cgroup_file || !limits ||
        limits->memory_max_bytes == 0 || limits->cpu_quota_us == 0 ||
        limits->cpu_period_us == 0 || limits->pids_max == 0) {
        errno = EINVAL;
        return -1;
    }
    memset(cgroup, 0, sizeof(*cgroup));

    char relative[PATH_MAX];
    if (get_current_cgroup(proc_cgroup_file, relative, sizeof(relative)) < 0) return -1;
    char parent[PATH_MAX];
    if (find_delegated_parent(cgroup_root, relative, parent, sizeof(parent)) < 0) return -1;

    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
    int n = snprintf(cgroup->path, sizeof(cgroup->path), "%s/pes-%ld-%lld",
                     parent, (long)getpid(), (long long)now.tv_nsec);
    if (n < 0 || (size_t)n >= sizeof(cgroup->path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (mkdir(cgroup->path, 0700) < 0) return -1;
    cgroup->active = true;

    char cpu_max[96];
    char memory_max[32];
    char pids_max[32];
    snprintf(cpu_max, sizeof(cpu_max), "%llu %llu", limits->cpu_quota_us, limits->cpu_period_us);
    snprintf(memory_max, sizeof(memory_max), "%llu", limits->memory_max_bytes);
    snprintf(pids_max, sizeof(pids_max), "%u", limits->pids_max);

    if (write_value(cgroup->path, "cpu.max", cpu_max) < 0 ||
        write_value(cgroup->path, "memory.max", memory_max) < 0 ||
        write_value(cgroup->path, "pids.max", pids_max) < 0) {
        int saved_errno = errno;
        if (phase2_cgroup_kill(cgroup) < 0 || phase2_cgroup_remove(cgroup) < 0) {
            return -1;
        }
        errno = saved_errno;
        return -1;
    }
    return 0;
}

int phase2_cgroup_attach(phase2_cgroup_t *cgroup, pid_t pid) {
    if (!cgroup || !cgroup->active || pid <= 0) {
        errno = EINVAL;
        return -1;
    }
    char value[32];
    snprintf(value, sizeof(value), "%ld", (long)pid);
    return write_value(cgroup->path, "cgroup.procs", value);
}

int phase2_cgroup_kill(phase2_cgroup_t *cgroup) {
    if (!cgroup || !cgroup->active) return 0;
    if (write_value(cgroup->path, "cgroup.kill", "1") < 0) return -1;
    char events_path[PATH_MAX];
    if (path_join(events_path, sizeof(events_path), cgroup->path, "cgroup.events") < 0) return -1;
    struct timespec start;
    if (clock_gettime(CLOCK_MONOTONIC, &start) < 0) return -1;
    uint64_t start_ms = (uint64_t)start.tv_sec * 1000ULL + (uint64_t)start.tv_nsec / 1000000ULL;
    for (;;) {
        char events[256];
        if (read_file(events_path, events, sizeof(events)) < 0) return -1;
        if (strstr(events, "populated 0") != NULL) return 0;
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
        uint64_t now_ms = (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
        if (now_ms - start_ms >= 2000ULL) {
            errno = ETIMEDOUT;
            return -1;
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 10000000L};
        while (nanosleep(&pause, &pause) < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
    }
}

int phase2_cgroup_remove(phase2_cgroup_t *cgroup) {
    if (!cgroup || !cgroup->active) return 0;
    if (rmdir(cgroup->path) < 0) return -1;
    cgroup->active = false;
    return 0;
}
