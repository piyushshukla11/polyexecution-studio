#ifndef PHASE2_USERNS_H
#define PHASE2_USERNS_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    int32_t result;
    int32_t errnum;
} phase2_userns_ready_t;

int phase2_userns_child_start(int ready_fd);
int phase2_userns_parent_map(pid_t child_pid, uint64_t deadline_ms, bool *gid_map_failed);

#endif
