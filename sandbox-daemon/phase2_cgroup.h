#ifndef PHASE2_CGROUP_H
#define PHASE2_CGROUP_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct {
    char path[4096];
    bool active;
} phase2_cgroup_t;

typedef struct {
    unsigned long long memory_max_bytes;
    unsigned long long cpu_quota_us;
    unsigned long long cpu_period_us;
    unsigned int pids_max;
} phase2_cgroup_limits_t;

void phase2_cgroup_limits_init(phase2_cgroup_limits_t *limits);
int phase2_cgroup_create(phase2_cgroup_t *cgroup,
                         const char *cgroup_root,
                         const char *proc_cgroup_file,
                         const phase2_cgroup_limits_t *limits);
int phase2_cgroup_attach(phase2_cgroup_t *cgroup, pid_t pid);
int phase2_cgroup_kill(phase2_cgroup_t *cgroup);
int phase2_cgroup_remove(phase2_cgroup_t *cgroup);

#endif
