#define _GNU_SOURCE

/* AGENT-WROTE-TIER-A: workspace handoff changes are owner-authorized. */

#include "phase2_rootfs.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#define PES_WORKSPACE_SOURCE_MAX (256U * 1024U)

static int valid_source_leaf(const char *name) {
    if (!name) return 0;
    if (strcmp(name, "source.c") == 0 ||
        strcmp(name, "source.py") == 0 ||
        strcmp(name, "source.java") == 0 ||
        strcmp(name, "Main.java") == 0 ||
        strcmp(name, "source.kt") == 0 ||
        strcmp(name, "source.js") == 0 ||
        strcmp(name, "target") == 0 ||
        strcmp(name, "target.jar") == 0) {
        return 1;
    }
    size_t len = strlen(name);
    if (len > 5 && strcmp(name + len - 5, ".java") == 0) {
        for (size_t i = 0; i < len - 5; i++) {
            char c = name[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
                return 0;
            }
        }
        return 1;
    }
    if (len > 6 && strcmp(name + len - 6, ".class") == 0) {
        for (size_t i = 0; i < len - 6; i++) {
            char c = name[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

static int valid_artifact_leaf(const char *name) {
    if (!name) return 1;
    if (strcmp(name, "target") == 0 || strcmp(name, "target.jar") == 0) {
        return 1;
    }
    size_t len = strlen(name);
    if (len > 6 && strcmp(name + len - 6, ".class") == 0) {
        for (size_t i = 0; i < len - 6; i++) {
            char c = name[i];
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
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
    uint64_t allowed_max = (uint64_t)max_source_bytes;
    size_t s_len = strlen(source_name);
    if (strcmp(source_name, "target") == 0 || strcmp(source_name, "target.jar") == 0 ||
        (s_len > 6 && strcmp(source_name + s_len - 6, ".class") == 0)) {
        allowed_max = 16ULL * 1024ULL * 1024ULL;
    }
    if (!S_ISREG(source_metadata.st_mode) ||
        source_metadata.st_uid != owner_uid || source_metadata.st_nlink != 1 ||
        (source_metadata.st_mode & 0777) != 0600 || source_metadata.st_size <= 0 ||
        (uint64_t)source_metadata.st_size > allowed_max) {
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

static uint32_t compute_crc32(const unsigned char *buf, size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320U & -(crc & 1U));
        }
    }
    return ~crc;
}

struct jar_entry_info {
    char name[256];
    uint32_t size;
    uint32_t crc;
    uint32_t offset;
};

static int package_workspace_classes_to_jar(int workspace_dir_fd, int artifact_fd,
                                            uid_t expected_owner, size_t max_artifact_bytes) {
    int dup_fd = dup(workspace_dir_fd);
    if (dup_fd < 0) return -1;
    DIR *dir = fdopendir(dup_fd);
    if (!dir) {
        close(dup_fd);
        return -1;
    }

    struct jar_entry_info entries[256];
    size_t count = 0;
    size_t total_bytes = 0;

    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        size_t nlen = strlen(de->d_name);
        if (nlen <= 6 || strcmp(de->d_name + nlen - 6, ".class") != 0) continue;
        if (count >= 256) {
            closedir(dir);
            errno = E2BIG;
            return -1;
        }

        struct stat st;
        if (fstatat(workspace_dir_fd, de->d_name, &st, AT_SYMLINK_NOFOLLOW) < 0 ||
            !S_ISREG(st.st_mode) || st.st_uid != expected_owner || st.st_size <= 0) {
            closedir(dir);
            errno = EACCES;
            return -1;
        }
        if ((size_t)st.st_size > max_artifact_bytes || total_bytes + (size_t)st.st_size > max_artifact_bytes) {
            closedir(dir);
            errno = EFBIG;
            return -1;
        }

        snprintf(entries[count].name, sizeof(entries[count].name), "%s", de->d_name);
        entries[count].size = (uint32_t)st.st_size;
        total_bytes += (size_t)st.st_size;
        count++;
    }

    if (count == 0) {
        closedir(dir);
        errno = ENOENT;
        return -1;
    }

    // Sort entries for deterministic archive order
    for (size_t i = 0; i < count - 1; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (strcmp(entries[i].name, entries[j].name) > 0) {
                struct jar_entry_info tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
        }
    }

    if (lseek(artifact_fd, 0, SEEK_SET) < 0 || ftruncate(artifact_fd, 0) < 0) {
        closedir(dir);
        return -1;
    }

    uint32_t cur_offset = 0;
    for (size_t i = 0; i < count; i++) {
        int efd = openat(workspace_dir_fd, entries[i].name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
        if (efd < 0) {
            closedir(dir);
            return -1;
        }
        unsigned char *buf = malloc(entries[i].size);
        if (!buf) {
            close(efd);
            closedir(dir);
            errno = ENOMEM;
            return -1;
        }
        size_t rd = 0;
        while (rd < entries[i].size) {
            ssize_t n = read(efd, buf + rd, entries[i].size - rd);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            rd += (size_t)n;
        }
        close(efd);
        if (rd != entries[i].size) {
            free(buf);
            closedir(dir);
            errno = EIO;
            return -1;
        }

        entries[i].crc = compute_crc32(buf, entries[i].size);
        entries[i].offset = cur_offset;
        uint16_t nlen = (uint16_t)strlen(entries[i].name);

        unsigned char lh[30];
        memset(lh, 0, sizeof(lh));
        lh[0] = 0x50; lh[1] = 0x4b; lh[2] = 0x03; lh[3] = 0x04; // 0x04034b50
        lh[4] = 20; // version needed (2.0)
        memcpy(lh + 14, &entries[i].crc, 4);
        memcpy(lh + 18, &entries[i].size, 4);
        memcpy(lh + 22, &entries[i].size, 4);
        memcpy(lh + 26, &nlen, 2);

        if (write_all(artifact_fd, (const char *)lh, 30) < 0 ||
            write_all(artifact_fd, entries[i].name, nlen) < 0 ||
            write_all(artifact_fd, (const char *)buf, entries[i].size) < 0) {
            free(buf);
            closedir(dir);
            return -1;
        }
        free(buf);
        cur_offset += 30 + (uint32_t)nlen + entries[i].size;
    }

    uint32_t cd_offset = cur_offset;
    uint32_t cd_size = 0;

    for (size_t i = 0; i < count; i++) {
        uint16_t nlen = (uint16_t)strlen(entries[i].name);
        unsigned char cdh[46];
        memset(cdh, 0, sizeof(cdh));
        cdh[0] = 0x50; cdh[1] = 0x4b; cdh[2] = 0x01; cdh[3] = 0x02; // 0x02014b50
        cdh[4] = 20; cdh[6] = 20; // version 2.0
        memcpy(cdh + 16, &entries[i].crc, 4);
        memcpy(cdh + 20, &entries[i].size, 4);
        memcpy(cdh + 24, &entries[i].size, 4);
        memcpy(cdh + 28, &nlen, 2);
        memcpy(cdh + 42, &entries[i].offset, 4);

        if (write_all(artifact_fd, (const char *)cdh, 46) < 0 ||
            write_all(artifact_fd, entries[i].name, nlen) < 0) {
            closedir(dir);
            return -1;
        }
        cd_size += 46 + (uint32_t)nlen;
    }

    unsigned char eocd[22];
    memset(eocd, 0, sizeof(eocd));
    eocd[0] = 0x50; eocd[1] = 0x4b; eocd[2] = 0x05; eocd[3] = 0x06; // 0x06054b50
    uint16_t num_ent = (uint16_t)count;
    memcpy(eocd + 8, &num_ent, 2);
    memcpy(eocd + 10, &num_ent, 2);
    memcpy(eocd + 12, &cd_size, 4);
    memcpy(eocd + 16, &cd_offset, 4);

    if (write_all(artifact_fd, (const char *)eocd, 22) < 0) {
        closedir(dir);
        return -1;
    }

    closedir(dir);
    return 0;
}

int phase2_rootfs_copy_artifact(int workspace_dir_fd, const char *artifact_name, int artifact_fd,
                                uid_t expected_owner, size_t max_artifact_bytes) {
    if (workspace_dir_fd < 0 || !artifact_name || !valid_artifact_leaf(artifact_name) ||
        artifact_fd < 0 || max_artifact_bytes == 0) {
        errno = EINVAL;
        return -1;
    }
    int source_fd = openat(workspace_dir_fd, artifact_name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (source_fd < 0) {
        if (strcmp(artifact_name, "target.jar") == 0) {
            return package_workspace_classes_to_jar(workspace_dir_fd, artifact_fd, expected_owner, max_artifact_bytes);
        }
        return -1;
    }
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

    const char *directories[] = {"proc", "tmp", "dev", "usr", "lib", "lib64", "bin", "sbin", "opt", "run", "workspace", ".oldroot"};
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

    const char *kotlin_home = getenv("PES_KOTLIN_HOME");
    if (!kotlin_home) {
        struct stat kh_st;
        if (stat("/home/zogratis/.local/share/mise/installs/kotlin/2.4.21/kotlinc", &kh_st) == 0 && S_ISDIR(kh_st.st_mode)) {
            kotlin_home = "/home/zogratis/.local/share/mise/installs/kotlin/2.4.21/kotlinc";
        }
    }
    if (kotlin_home && kotlin_home[0] == '/') {
        char opt_path[PATH_MAX];
        if (snprintf(opt_path, sizeof(opt_path), "%s/opt", root) < (int)sizeof(opt_path)) {
            make_directory(opt_path);
            bind_readonly_directory(root, kotlin_home, "opt/kotlinc");
        }
    } else {
        bind_readonly_directory(root, "/opt", "opt");
    }

    const char *node_home = getenv("PES_NODE_HOME");
    if (!node_home) {
        struct stat nh_st;
        if (stat("/home/zogratis/.local/share/mise/installs/node/26.8.2", &nh_st) == 0 && S_ISDIR(nh_st.st_mode)) {
            node_home = "/home/zogratis/.local/share/mise/installs/node/26.8.2";
        }
    }
    if (node_home && node_home[0] == '/') {
        char opt_path[PATH_MAX];
        if (snprintf(opt_path, sizeof(opt_path), "%s/opt", root) < (int)sizeof(opt_path)) {
            make_directory(opt_path);
            bind_readonly_directory(root, node_home, "opt/node");
        }
    }

    int is_system_binary = (strncmp(host_binary, "/usr/", 5) == 0 ||
                            strncmp(host_binary, "/bin/", 5) == 0 ||
                            strncmp(host_binary, "/sbin/", 6) == 0 ||
                            strncmp(host_binary, "/opt/", 5) == 0);
    char target_host_path[PATH_MAX];
    char temp_path[PATH_MAX];
    if (is_system_binary) {
        char check_path[PATH_MAX];
        if (strncmp(host_binary, "/opt/", 5) == 0) {
            snprintf(check_path, sizeof(check_path), "%s%s", root, host_binary);
        } else {
            snprintf(check_path, sizeof(check_path), "%s", host_binary);
        }
        struct stat sys_st;
        if (stat(check_path, &sys_st) < 0 || !S_ISREG(sys_st.st_mode) || (sys_st.st_mode & 0111) == 0) {
            errno = ENOENT;
            return -1;
        }
        if (snprintf(target_path, target_path_size, "%s", host_binary) >= (int)target_path_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
    } else {
        if (snprintf(target_host_path, sizeof(target_host_path), "%s/run/target", root) >= (int)sizeof(target_host_path) ||
            snprintf(target_path, target_path_size, "/run/target") >= (int)target_path_size) {
            errno = ENAMETOOLONG;
            return -1;
        }
    }
    if (snprintf(temp_path, sizeof(temp_path), "%s/tmp", root) >= (int)sizeof(temp_path)) {
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
    size_t max_pop_bytes = PES_WORKSPACE_SOURCE_MAX;
    if (workspace_source_name) {
        size_t wsn_len = strlen(workspace_source_name);
        if (strcmp(workspace_source_name, "target") == 0 || strcmp(workspace_source_name, "target.jar") == 0 ||
            (wsn_len > 6 && strcmp(workspace_source_name + wsn_len - 6, ".class") == 0)) {
            max_pop_bytes = 16U * 1024U * 1024U;
        }
    }
    if (workspace_source_fd >= 0 && phase2_workspace_populate(workspace_fd, workspace_source_fd,
            workspace_source_name, 0, workspace_uid, workspace_gid, max_pop_bytes) < 0) {
        int saved_errno = errno;
        close(workspace_fd);
        errno = saved_errno;
        return -1;
    }
    if (close(workspace_fd) < 0) return -1;
    if (!is_system_binary && copy_target_binary(host_binary, target_host_path) < 0) return -1;
    if (chdir(root) < 0) return -1;
    if (syscall(SYS_pivot_root, ".", ".oldroot") < 0) return -1;
    if (chdir("/") < 0) return -1;
    return 0;
}

int phase2_rootfs_mount_proc(phase2_procfs_step_t *failed_step_out) {
    if (!failed_step_out) {
        errno = EINVAL;
        return -1;
    }

    *failed_step_out = PHASE2_PROCFS_STEP_MOUNT;
    if (mount("proc", "/proc", "proc", MS_NOSUID | MS_NODEV | MS_NOEXEC, "subset=pid") < 0) return -1;
    *failed_step_out = PHASE2_PROCFS_STEP_OLDROOT_UMOUNT;
    if (umount2("/.oldroot", MNT_DETACH) < 0) return -1;
    *failed_step_out = PHASE2_PROCFS_STEP_OLDROOT_RMDIR;
    if (rmdir("/.oldroot") < 0) return -1;
    *failed_step_out = PHASE2_PROCFS_STEP_CHDIR_TMP;
    if (chdir("/tmp") < 0) return -1;
    return 0;
}
