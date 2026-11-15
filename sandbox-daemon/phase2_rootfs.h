#ifndef PHASE2_ROOTFS_H
#define PHASE2_ROOTFS_H

#include <stddef.h>

int phase2_rootfs_create_mountpoint(char *path, size_t path_size);
int phase2_rootfs_remove_mountpoint(const char *path);
int phase2_rootfs_prepare(const char *host_binary, const char *root, char *target_path, size_t target_path_size);
int phase2_rootfs_mount_proc(void);

#endif
