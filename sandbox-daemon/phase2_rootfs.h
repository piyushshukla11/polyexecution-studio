#ifndef PHASE2_ROOTFS_H
#define PHASE2_ROOTFS_H

#include <stddef.h>
#include <sys/types.h>

typedef enum {
    PHASE2_PROCFS_STEP_MOUNT = 1,
    PHASE2_PROCFS_STEP_OLDROOT_UMOUNT,
    PHASE2_PROCFS_STEP_OLDROOT_RMDIR,
    PHASE2_PROCFS_STEP_CHDIR_TMP
} phase2_procfs_step_t;

int phase2_rootfs_create_mountpoint(char *path, size_t path_size);
int phase2_rootfs_remove_mountpoint(const char *path);
int phase2_rootfs_prepare(const char *host_binary, const char *root, char *target_path, size_t target_path_size,
                          int workspace_source_fd, const char *workspace_source_name,
                          uid_t workspace_uid, gid_t workspace_gid);
int phase2_workspace_open(const char *workspace_path, const char *source_name,
                          const char *artifact_name, uid_t owner_uid,
                          size_t max_source_bytes, int *source_fd_out,
                          int *artifact_fd_out);
int phase2_workspace_populate(int workspace_dir_fd, int source_fd, const char *source_name,
                              uid_t source_owner_uid, uid_t owner_uid, gid_t owner_gid,
                              size_t max_source_bytes);
int phase2_rootfs_copy_artifact(int workspace_dir_fd, const char *artifact_name, int artifact_fd,
                                uid_t expected_owner, size_t max_artifact_bytes);
int phase2_rootfs_mount_proc(phase2_procfs_step_t *failed_step_out);

#endif
