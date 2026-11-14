#define _GNU_SOURCE

#include "phase2_rootfs.h"

#include <errno.h>
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

int phase2_rootfs_prepare(const char *host_binary, const char *root, char *target_path, size_t target_path_size) {
    if (!host_binary || !root || !target_path || target_path_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) < 0) return -1;
    if (mount("tmpfs", root, "tmpfs", MS_NOSUID | MS_NODEV, "size=128m,mode=0755") < 0) return -1;

    const char *directories[] = {"proc", "tmp", "dev", "usr", "lib", "lib64", "bin", "sbin", "run", ".oldroot"};
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
    if (chmod(temp_path, 01777) < 0) return -1;
    if (copy_target_binary(host_binary, target_host_path) < 0) return -1;
    if (chdir(root) < 0) return -1;
    if (syscall(SYS_pivot_root, ".", ".oldroot") < 0) return -1;
    if (chdir("/") < 0) return -1;
    if (umount2("/.oldroot", MNT_DETACH) < 0) return -1;
    if (rmdir("/.oldroot") < 0) return -1;
    if (chdir("/tmp") < 0) return -1;
    return 0;
}

int phase2_rootfs_mount_proc(void) {
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL) < 0) return -1;
    return 0;
}
