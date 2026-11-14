#define _GNU_SOURCE

#include "phase2_userns.h"

#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int write_ready(int fd, int result, int errnum) {
    phase2_userns_ready_t ready = {.result = result, .errnum = errnum};
    const char *bytes = (const char *)&ready;
    size_t written = 0;
    while (written < sizeof(ready)) {
        ssize_t n = write(fd, bytes + written, sizeof(ready) - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return -1;
        written += (size_t)n;
    }
    return 0;
}

int phase2_userns_child_start(int ready_fd) {
    if (unshare(CLONE_NEWUSER) < 0) {
        int saved_errno = errno;
        (void)write_ready(ready_fd, -1, saved_errno);
        errno = saved_errno;
        return -1;
    }
    return write_ready(ready_fd, 0, 0);
}

static int find_subid_range(const char *file_path, const char *name, unsigned long numeric_id,
                            unsigned long *start_out) {
    FILE *file = fopen(file_path, "re");
    if (!file) return -1;
    char *line = NULL;
    size_t capacity = 0;
    int result = -1;
    while (getline(&line, &capacity, file) >= 0) {
        char entry_name[256];
        unsigned long start = 0;
        unsigned long count = 0;
        if (sscanf(line, "%255[^:]:%lu:%lu", entry_name, &start, &count) != 3) continue;
        char numeric_name[32];
        snprintf(numeric_name, sizeof(numeric_name), "%lu", numeric_id);
        if ((strcmp(entry_name, name) == 0 || strcmp(entry_name, numeric_name) == 0) &&
            count > 0 && start <= 0xFFFFFFFFUL) {
            *start_out = start;
            result = 0;
            break;
        }
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

static int wait_helper_until(pid_t pid, uint64_t deadline_ms) {
    int status = 0;
    for (;;) {
        pid_t result = waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return 0;
            errno = EACCES;
            return -1;
        }
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        struct timespec now;
        if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) return -1;
        uint64_t now_ms = (uint64_t)now.tv_sec * 1000ULL + (uint64_t)now.tv_nsec / 1000000ULL;
        if (now_ms >= deadline_ms) {
            if (kill(pid, SIGKILL) < 0 && errno != ESRCH) return -1;
            pid_t waited;
            do {
                waited = waitpid(pid, &status, 0);
            } while (waited < 0 && errno == EINTR);
            if (waited != pid) return -1;
            errno = ETIMEDOUT;
            return -1;
        }
        struct timespec pause = {.tv_sec = 0, .tv_nsec = 5000000L};
        while (nanosleep(&pause, &pause) < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
    }
}

static int run_map_helper(const char *helper, pid_t pid,
                          unsigned long inside_primary, unsigned long outside_primary,
                          unsigned long inside_unprivileged, unsigned long outside_unprivileged,
                          uint64_t deadline_ms) {
    char pid_arg[32], inside_primary_arg[32], outside_primary_arg[32];
    char inside_unprivileged_arg[32], outside_unprivileged_arg[32];
    snprintf(pid_arg, sizeof(pid_arg), "%ld", (long)pid);
    snprintf(inside_primary_arg, sizeof(inside_primary_arg), "%lu", inside_primary);
    snprintf(outside_primary_arg, sizeof(outside_primary_arg), "%lu", outside_primary);
    snprintf(inside_unprivileged_arg, sizeof(inside_unprivileged_arg), "%lu", inside_unprivileged);
    snprintf(outside_unprivileged_arg, sizeof(outside_unprivileged_arg), "%lu", outside_unprivileged);
    char *const argv[] = { (char *)helper, pid_arg,
                           inside_primary_arg, outside_primary_arg, "1",
                           inside_unprivileged_arg, outside_unprivileged_arg, "1", NULL };
    char *const envp[] = { NULL };

    pid_t helper_pid = fork();
    if (helper_pid < 0) return -1;
    if (helper_pid == 0) {
        execve(helper, argv, envp);
        _exit(127);
    }
    return wait_helper_until(helper_pid, deadline_ms);
}

static int verify_id_map(pid_t pid, const char *map_name,
                         unsigned long outside_primary, unsigned long outside_unprivileged) {
    char path[128];
    if (snprintf(path, sizeof(path), "/proc/%ld/%s", (long)pid, map_name) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    FILE *file = fopen(path, "re");
    if (!file) return -1;
    char first[128], second[128], extra[128];
    unsigned long inside1 = 0, outside1 = 0, count1 = 0;
    unsigned long inside2 = 0, outside2 = 0, count2 = 0;
    char trailing = '\0';
    bool exact = fgets(first, sizeof(first), file) != NULL &&
                 fgets(second, sizeof(second), file) != NULL &&
                 sscanf(first, "%lu %lu %lu %c", &inside1, &outside1, &count1, &trailing) == 3 &&
                 sscanf(second, "%lu %lu %lu %c", &inside2, &outside2, &count2, &trailing) == 3;
    while (exact && fgets(extra, sizeof(extra), file) != NULL) {
        for (char *p = extra; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                exact = false;
                break;
            }
        }
    }
    int saved_errno = errno;
    if (fclose(file) < 0) {
        exact = false;
        saved_errno = errno;
    }
    if (!exact || inside1 != 0 || outside1 != outside_primary || count1 != 1 ||
        inside2 != 65534 || outside2 != outside_unprivileged || count2 != 1) {
        errno = saved_errno ? saved_errno : EPROTO;
        return -1;
    }
    return 0;
}

static int verify_setgroups_allow(pid_t pid) {
    char path[128];
    if (snprintf(path, sizeof(path), "/proc/%ld/setgroups", (long)pid) >= (int)sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    FILE *file = fopen(path, "re");
    if (!file) return -1;
    char state[16];
    int result = fscanf(file, "%15s", state);
    int saved_errno = errno;
    if (fclose(file) < 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result != 1 || strcmp(state, "allow") != 0) {
        errno = saved_errno ? saved_errno : EPERM;
        return -1;
    }
    return 0;
}

int phase2_userns_parent_map(pid_t child_pid, uint64_t deadline_ms, bool *gid_map_failed) {
    if (child_pid <= 0 || !gid_map_failed) {
        errno = EINVAL;
        return -1;
    }
    *gid_map_failed = false;
    struct passwd *pw = getpwuid(getuid());
    if (!pw || !pw->pw_name) {
        errno = ENOENT;
        return -1;
    }
    unsigned long subuid_start = 0;
    unsigned long subgid_start = 0;
    if (find_subid_range("/etc/subuid", pw->pw_name, (unsigned long)getuid(), &subuid_start) < 0) return -1;
    if (find_subid_range("/etc/subgid", pw->pw_name, (unsigned long)getgid(), &subgid_start) < 0) {
        *gid_map_failed = true;
        return -1;
    }
    if ((unsigned long)getuid() >= subuid_start && (unsigned long)getuid() - subuid_start <= 65533UL) {
        errno = EINVAL;
        return -1;
    }
    if ((unsigned long)getgid() >= subgid_start && (unsigned long)getgid() - subgid_start <= 65533UL) {
        errno = EINVAL;
        return -1;
    }

    if (run_map_helper("/usr/bin/newuidmap", child_pid, 0, (unsigned long)getuid(),
                       65534, subuid_start, deadline_ms) < 0) return -1;
    if (verify_id_map(child_pid, "uid_map", (unsigned long)getuid(), subuid_start) < 0) return -1;
    if (run_map_helper("/usr/bin/newgidmap", child_pid, 0, (unsigned long)getgid(),
                       65534, subgid_start, deadline_ms) < 0) {
        *gid_map_failed = true;
        return -1;
    }
    if (verify_id_map(child_pid, "gid_map", (unsigned long)getgid(), subgid_start) < 0 ||
        verify_setgroups_allow(child_pid) < 0) {
        *gid_map_failed = true;
        return -1;
    }
    return 0;
}
