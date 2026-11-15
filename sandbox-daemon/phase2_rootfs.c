#define _GNU_SOURCE

/* AGENT-WROTE-TIER-A: workspace handoff changes are owner-authorized. */

#include "phase2_rootfs.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/mount.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PES_WORKSPACE_SOURCE_MAX (256U * 1024U)

static int valid_source_leaf(const char *name) {
    return name && (strcmp(name, "source.c") == 0 || strcmp(name, "source.py") == 0);
}

static int valid_artifact_leaf(const char *name) {
    return !name || strcmp(name, "target") == 0;
}

static int write_all(int fd, const char *buffer, size_t length) {
    size_t written = 0;
    while (written < length) {
        ssize_t count = write(fd, buffer + written, length - written);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            if (count == 0) errno = EIO;
            return -1;
        }
        written += (size_t)count;
    }
    return 0;
}

static int copy_fd_bounded(int source_fd, int target_fd, size_t max_bytes) {
    char buffer[8192];
    size_t total = 0;
    if (lseek(source_fd, 0, SEEK_SET) < 0) return -1;
    for (;;) {
        ssize_t count = read(source_fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) return -1;
        if (count == 0) return 0;
        if ((size_t)count > max_bytes - total) {
            errno = EFBIG;
            return -1;
        }
        if (write_all(target_fd, buffer, (size_t)count) < 0) return -1;
        total += (size_t)count;
    }
}

static int validate_workspace_entries(int dir_fd, const char *source_name) {
    int duplicate_fd = dup(dir_fd);
    if (duplicate_fd < 0) return -1;
    DIR *directory = fdopendir(duplicate_fd);
    if (!directory) {
        int saved_errno = errno;
        close(duplicate_fd);
        errno = saved_errno;
        return -1;
    }
    int result = 0;
    struct dirent *entry;
    errno = 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 ||
            strcmp(entry->d_name, source_name) == 0) continue;
        errno = EINVAL;
        result = -1;
        break;
    }
    int saved_errno = errno;
    if (closedir(directory) < 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result < 0) errno = saved_errno ? saved_errno : EINVAL;
    return result;
}

int phase2_workspace_open(const char *workspace_path, const char *source_name,
                          const char *artifact_name, uid_t owner_uid,
                          size_t max_source_bytes, int *source_fd_out,
                          int *artifact_fd_out) {
    if (!workspace_path || !valid_source_leaf(source_name) || !valid_artifact_leaf(artifact_name) ||
        !source_fd_out || !artifact_fd_out || max_source_bytes == 0 ||
        strncmp(workspace_path, "/tmp/pes-job-", sizeof("/tmp/pes-job-") - 1U) != 0) {
        errno = EINVAL;
        return -1;
    }
    *source_fd_out = -1;
    *artifact_fd_out = -1;
    const char *leaf = workspace_path + sizeof("/tmp/") - 1U;
    if (strchr(leaf, '/') || leaf[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    for (const unsigned char *p = (const unsigned char *)leaf; *p; p++) {
        if (!( (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
               (*p >= '0' && *p <= '9') || *p == '-' || *p == '_')) {
            errno = EINVAL;
            return -1;
        }
    }

    int tmp_fd = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (tmp_fd < 0) return -1;
    int dir_fd = openat(tmp_fd, leaf, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    int saved_errno = errno;
    if (close(tmp_fd) < 0 && dir_fd >= 0) {
        saved_errno = errno;
        close(dir_fd);
        errno = saved_errno;
        return -1;
    }
    if (dir_fd < 0) {
        errno = saved_errno;
        return -1;
    }

    struct stat directory_metadata;
    if (fstat(dir_fd, &directory_metadata) < 0) {
        saved_errno = errno;
        close(dir_fd);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISDIR(directory_metadata.st_mode) || directory_metadata.st_uid != owner_uid ||
        (directory_metadata.st_mode & 0777) != 0700) {
        close(dir_fd);
        errno = EACCES;
        return -1;
    }
    if (validate_workspace_entries(dir_fd, source_name) < 0) {
        saved_errno = errno;
        close(dir_fd);
        errno = saved_errno;
        return -1;
    }

    int source_fd = openat(dir_fd, source_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (source_fd < 0) {
        saved_errno = errno;
        close(dir_fd);
        errno = saved_errno;
        return -1;
    }
    struct stat source_metadata;
    if (fstat(source_fd, &source_metadata) < 0) {
        saved_errno = errno;
        close(source_fd);
        close(dir_fd);
        errno = saved_errno;
        return -1;
    }
    if (!S_ISREG(source_metadata.st_mode) ||
        source_metadata.st_uid != owner_uid || source_metadata.st_nlink != 1 ||
        (source_metadata.st_mode & 0777) != 0600 || source_metadata.st_size <= 0 ||
        (uint64_t)source_metadata.st_size > (uint64_t)max_source_bytes) {
        close(source_fd);
        close(dir_fd);
        errno = EACCES;
        return -1;
    }
    int artifact_fd = -1;
    if (artifact_name) {
        artifact_fd = openat(dir_fd, artifact_name,
                             O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (artifact_fd < 0) {
            saved_errno = errno;
            close(source_fd);
            close(dir_fd);
            errno = saved_errno;
            return -1;
        }
        struct stat artifact_metadata;
        if (fstat(artifact_fd, &artifact_metadata) < 0) {
            saved_errno = errno;
            close(artifact_fd);
            unlinkat(dir_fd, artifact_name, 0);
            close(source_fd);
            close(dir_fd);
            errno = saved_errno;
            return -1;
        }
        if (!S_ISREG(artifact_metadata.st_mode) || artifact_metadata.st_uid != owner_uid ||
            artifact_metadata.st_nlink != 1 || (artifact_metadata.st_mode & 0777) != 0600) {
            close(artifact_fd);
            unlinkat(dir_fd, artifact_name, 0);
            close(source_fd);
            close(dir_fd);
            errno = EACCES;
            return -1;
        }
    }
    if (close(dir_fd) < 0) {
        saved_errno = errno;
        close(source_fd);
        if (artifact_fd >= 0) close(artifact_fd);
        errno = saved_errno;
        return -1;
    }
    *source_fd_out = source_fd;
    *artifact_fd_out = artifact_fd;
    return 0;
}

int phase2_workspace_populate(int workspace_dir_fd, int source_fd, const char *source_name,
                              uid_t source_owner_uid, uid_t owner_uid, gid_t owner_gid,
                              size_t max_source_bytes) {
    if (workspace_dir_fd < 0 || source_fd < 0 || !valid_source_leaf(source_name) || max_source_bytes == 0) {
        errno = EINVAL;
        return -1;
    }
    struct stat directory_metadata, source_metadata;
    if (fstat(workspace_dir_fd, &directory_metadata) < 0 || fstat(source_fd, &source_metadata) < 0) return -1;
    if (!S_ISDIR(directory_metadata.st_mode) || directory_metadata.st_uid != owner_uid ||
        directory_metadata.st_gid != owner_gid || (directory_metadata.st_mode & 0777) != 0700 ||
        !S_ISREG(source_metadata.st_mode) || source_metadata.st_uid != source_owner_uid ||
        source_metadata.st_nlink != 1 || (source_metadata.st_mode & 0777) != 0600 ||
        source_metadata.st_size <= 0 || (uint64_t)source_metadata.st_size > (uint64_t)max_source_bytes) {
        errno = EACCES;
        return -1;
    }
    int target_fd = openat(workspace_dir_fd, source_name,
                           O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (target_fd < 0) return -1;
    int result = 0;
    int saved_errno = 0;
    struct stat target_metadata;
    if (fchown(target_fd, owner_uid, owner_gid) < 0 || fchmod(target_fd, 0400) < 0 ||
        fstat(target_fd, &target_metadata) < 0 || target_metadata.st_uid != owner_uid ||
        target_metadata.st_gid != owner_gid || (target_metadata.st_mode & 0777) != 0400 ||
        copy_fd_bounded(source_fd, target_fd, max_source_bytes) < 0) {
        result = -1;
        saved_errno = errno ? errno : EACCES;
    }
    if (close(target_fd) < 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result < 0) {
        if (unlinkat(workspace_dir_fd, source_name, 0) < 0 && errno != ENOENT) return -1;
        errno = saved_errno;
    }
    return result;
}

int phase2_rootfs_copy_artifact(int workspace_dir_fd, int artifact_fd,
                                uid_t expected_owner, size_t max_artifact_bytes) {
    if (workspace_dir_fd < 0 || artifact_fd < 0 || max_artifact_bytes == 0) {
        errno = EINVAL;
        return -1;
    }
    int source_fd = openat(workspace_dir_fd, "target", O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (source_fd < 0) return -1;
    struct stat metadata;
    if (fstat(source_fd, &metadata) < 0 || !S_ISREG(metadata.st_mode) || metadata.st_uid != expected_owner ||
        metadata.st_nlink != 1 || metadata.st_size <= 0 || (uint64_t)metadata.st_size > max_artifact_bytes) {
        int saved_errno = errno;
        if (saved_errno == 0) saved_errno = EINVAL;
        close(source_fd);
        errno = saved_errno;
        return -1;
    }
    if (lseek(artifact_fd, 0, SEEK_SET) < 0 || ftruncate(artifact_fd, 0) < 0 ||
        copy_fd_bounded(source_fd, artifact_fd, max_artifact_bytes) < 0) {
        int saved_errno = errno;
        close(source_fd);
        errno = saved_errno;
        return -1;
    }
    return close(source_fd);
}

static int make_directory(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
    return -1;
}

static int copy_target_binary(const char *source, const char *target) {
    int source_fd = open(source, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (source_fd < 0) return -1;
    struct stat metadata;
    if (fstat(source_fd, &metadata) < 0) {
        int saved_errno = errno;
        if (close(source_fd) < 0) return -1;
        errno = saved_errno;
        return -1;
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_size <= 0 || metadata.st_size > 64 * 1024 * 1024) {
        if (close(source_fd) < 0) return -1;
        errno = EINVAL;
        return -1;
    }
    int target_fd = open(target, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0555);
    if (target_fd < 0) {
        if (close(source_fd) < 0) return -1;
        return -1;
    }
    char buffer[16384];
    int result = 0;
    for (;;) {
        ssize_t count = read(source_fd, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            result = -1;
            break;
        }
        if (count == 0) break;
        size_t written = 0;
        while (written < (size_t)count) {
            ssize_t n = write(target_fd, buffer + written, (size_t)count - written);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) {
                if (n == 0) errno = EIO;
                result = -1;
                break;
            }
            written += (size_t)n;
        }
        if (result < 0) break;
    }
    int saved_errno = errno;
    if (fchmod(target_fd, 0555) < 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (close(target_fd) < 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (close(source_fd) < 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result < 0) {
        if (unlink(target) < 0 && errno != ENOENT) return -1;
        errno = saved_errno;
    }
    return result;
}

static int bind_readonly_directory(const char *root, const char *source, const char *leaf) {
    struct stat metadata;
    if (stat(source, &metadata) < 0) {
        if (errno == ENOENT) return 0;
        return -1;
    }
    if (!S_ISDIR(metadata.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    char target[PATH_MAX];
    if (snprintf(target, sizeof(target), "%s/%s", root, leaf) >= (int)sizeof(target)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (make_directory(target) < 0) return -1;
    if (mount(source, target, NULL, MS_BIND, NULL) < 0) return -1;
    if (mount(NULL, target, NULL, MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV, NULL) < 0) return -1;
    return 0;
}

int phase2_rootfs_create_mountpoint(char *path, size_t path_size) {
    if (!path || path_size < sizeof("/tmp/pes-root-XXXXXX")) {
        errno = EINVAL;
        return -1;
    }
    if (snprintf(path, path_size, "/tmp/pes-root-XXXXXX") >= (int)path_size || !mkdtemp(path)) return -1;
    return 0;
}

int phase2_rootfs_remove_mountpoint(const char *path) {
    if (!path || path[0] != '/') {
        errno = EINVAL;
        return -1;
    }
    return rmdir(path);
}

int phase2_rootfs_prepare(const char *host_binary, const char *root, char *target_path, size_t target_path_size,
                          int workspace_source_fd, const char *workspace_source_name,
                          uid_t workspace_uid, gid_t workspace_gid) {
    if (!host_binary || !root || !target_path || target_path_size == 0 ||
        ((workspace_source_fd < 0) != (workspace_source_name == NULL))) {
        errno = EINVAL;
        return -1;
    }
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) return -1;
    if (mount("tmpfs", root, "tmpfs", MS_NOSUID | MS_NODEV, "size=128m,mode=0755") < 0) return -1;

    const char *directories[] = {"proc", "tmp", "dev", "usr", "lib", "lib64", "bin", "sbin", "run", "workspace", ".oldroot"};
    for (size_t i = 0; i < sizeof(directories) / sizeof(directories[0]); i++) {
        char path[PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", root, directories[i]) >= (int)sizeof(path)) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (make_directory(path) < 0) return -1;
    }

    if (bind_readonly_directory(root, "/usr", "usr") < 0 ||
        bind_readonly_directory(root, "/lib", "lib") < 0 ||
        bind_readonly_directory(root, "/lib64", "lib64") < 0 ||
        bind_readonly_directory(root, "/bin", "bin") < 0 ||
        bind_readonly_directory(root, "/sbin", "sbin") < 0) return -1;

    char target_host_path[PATH_MAX];
    char temp_path[PATH_MAX];
    if (snprintf(target_host_path, sizeof(target_host_path), "%s/run/target", root) >= (int)sizeof(target_host_path) ||
        snprintf(temp_path, sizeof(temp_path), "%s/tmp", root) >= (int)sizeof(temp_path) ||
        snprintf(target_path, target_path_size, "/run/target") >= (int)target_path_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    char workspace_path[PATH_MAX];
    if (snprintf(workspace_path, sizeof(workspace_path), "%s/workspace", root) >= (int)sizeof(workspace_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (chmod(temp_path, 01777) < 0 || chown(workspace_path, workspace_uid, workspace_gid) < 0 ||
        chmod(workspace_path, 0700) < 0) return -1;
    int workspace_fd = open(workspace_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (workspace_fd < 0) return -1;
    struct stat workspace_metadata;
    if (fstat(workspace_fd, &workspace_metadata) < 0 || workspace_metadata.st_uid != workspace_uid ||
        workspace_metadata.st_gid != workspace_gid || (workspace_metadata.st_mode & 0777) != 0700) {
        int saved_errno = errno ? errno : EACCES;
        close(workspace_fd);
        errno = saved_errno;
        return -1;
    }
    if (workspace_source_fd >= 0 && phase2_workspace_populate(workspace_fd, workspace_source_fd,
            workspace_source_name, 0, workspace_uid, workspace_gid, PES_WORKSPACE_SOURCE_MAX) < 0) {
        int saved_errno = errno;
        close(workspace_fd);
        errno = saved_errno;
        return -1;
    }
    if (close(workspace_fd) < 0) return -1;
    if (copy_target_binary(host_binary, target_host_path) < 0) return -1;
    if (chdir(root) < 0) return -1;
    if (syscall(SYS_pivot_root, ".", ".oldroot") < 0) return -1;
    if (chdir("/") < 0) return -1;
    return 0;
}

int phase2_rootfs_mount_proc(void) {
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) return -1;
    if (umount2("/.oldroot", MNT_DETACH) < 0) return -1;
    if (rmdir("/.oldroot") < 0) return -1;
    if (chdir("/tmp") < 0) return -1;
    return 0;
}
