#define _GNU_SOURCE
#include "../phase2_rootfs.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s (errno=%d: %s)\n", m, errno, strerror(errno)); return -1; } } while (0)

static int write_file_at(int dirfd, const char *name, const char *contents, mode_t mode) {
    int fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, mode);
    if (fd < 0) return -1;
    size_t length = strlen(contents), offset = 0;
    while (offset < length) {
        ssize_t n = write(fd, contents + offset, length - offset);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { int e = n == 0 ? EIO : errno; close(fd); errno = e; return -1; }
        offset += (size_t)n;
    }
    return close(fd);
}

static int cleanup_dir(const char *path, const char *first, const char *second) {
    char item[256];
    if (first) { snprintf(item, sizeof(item), "%s/%s", path, first); unlink(item); }
    if (second) { snprintf(item, sizeof(item), "%s/%s", path, second); unlink(item); }
    return rmdir(path);
}

static int test_handoff(void) {
    char host[] = "/tmp/pes-job-test-XXXXXX";
    CHECK(mkdtemp(host) != NULL && chmod(host, 0700) == 0, "create private job workspace");
    int hostfd = open(host, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(hostfd >= 0, "open job workspace");
    CHECK(write_file_at(hostfd, "source.c", "int main(void) { return 0; }\n", 0600) == 0, "write source");
    int sourcefd = -1, artifactfd = -1;
    CHECK(phase2_workspace_open(host, "source.c", "target", geteuid(), 256U * 1024U,
                                &sourcefd, &artifactfd) == 0, "validate source and create private artifact destination");
    struct stat st;
    CHECK(fstat(artifactfd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
          (st.st_mode & 0777) == 0600, "artifact destination is private and worker-owned");

    char root[] = "/tmp/pes-rootws-test-XXXXXX";
    CHECK(mkdtemp(root) != NULL && chmod(root, 0700) == 0, "create isolated workspace fixture");
    int rootfd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(rootfd >= 0, "open isolated workspace fixture");
    CHECK(phase2_workspace_populate(rootfd, sourcefd, "source.c", geteuid(), geteuid(), getegid(),
                                    256U * 1024U) == 0, "copy source with explicit mapped identity");
    int copied = openat(rootfd, "source.c", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(copied >= 0 && fstat(copied, &st) == 0 && st.st_uid == geteuid() && st.st_gid == getegid() &&
          (st.st_mode & 0777) == 0400, "source is readable only by mapped sandbox identity");
    char source[64] = {0};
    CHECK(read(copied, source, sizeof(source) - 1) > 0 && strcmp(source, "int main(void) { return 0; }\n") == 0,
          "sandbox workspace contains exact source");
    CHECK(close(copied) == 0, "close copied source");

    CHECK(symlinkat("/etc/passwd", rootfd, "target") == 0, "create artifact symlink fixture");
    CHECK(phase2_rootfs_copy_artifact(rootfd, artifactfd, geteuid(), 1024) < 0,
          "reject artifact symlink instead of following it");
    CHECK(unlinkat(rootfd, "target", 0) == 0, "remove artifact symlink fixture");
    CHECK(write_file_at(rootfd, "target", "compiled-artifact", 0700) == 0, "create sandbox artifact fixture");
    CHECK(phase2_rootfs_copy_artifact(rootfd, artifactfd, geteuid(), 1024) == 0,
          "copy bounded artifact back through pre-opened fd");
    int returned = openat(hostfd, "target", O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(returned >= 0, "artifact exists in worker workspace");
    char artifact[64] = {0};
    CHECK(read(returned, artifact, sizeof(artifact) - 1) == (ssize_t)strlen("compiled-artifact") &&
          strcmp(artifact, "compiled-artifact") == 0, "worker receives exact compiled artifact bytes");
    CHECK(close(returned) == 0, "close returned artifact");

    CHECK(close(rootfd) == 0 && close(sourcefd) == 0 && close(artifactfd) == 0 && close(hostfd) == 0,
          "close workspace descriptors");
    CHECK(cleanup_dir(root, "source.c", "target") == 0, "tear down isolated workspace");
    CHECK(cleanup_dir(host, "source.c", "target") == 0, "tear down job workspace");
    return 0;
}

static int test_rejections(void) {
    char host[] = "/tmp/pes-job-reject-XXXXXX";
    CHECK(mkdtemp(host) != NULL && chmod(host, 0700) == 0, "create validation workspace");
    int dirfd = open(host, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    CHECK(dirfd >= 0 && write_file_at(dirfd, "source.c", "source\n", 0600) == 0, "write validation source");
    int sourcefd = -1, artifactfd = -1;
    CHECK(phase2_workspace_open(host, "../source.c", NULL, geteuid(), 256U * 1024U, &sourcefd, &artifactfd) < 0,
          "reject path traversal");
    CHECK(phase2_workspace_open(host, "source.c", NULL, geteuid(), 1, &sourcefd, &artifactfd) < 0,
          "reject oversized source");
    CHECK(chmod(host, 0755) == 0, "make directory permissions unsafe");
    CHECK(phase2_workspace_open(host, "source.c", NULL, geteuid(), 256U * 1024U, &sourcefd, &artifactfd) < 0,
          "reject accessible workspace");
    CHECK(chmod(host, 0700) == 0, "restore workspace permissions");
    CHECK(write_file_at(dirfd, "unexpected", "x", 0600) == 0, "add unexpected entry");
    CHECK(phase2_workspace_open(host, "source.c", NULL, geteuid(), 256U * 1024U, &sourcefd, &artifactfd) < 0,
          "reject unexpected workspace entry");
    CHECK(unlinkat(dirfd, "unexpected", 0) == 0, "remove unexpected entry");
    CHECK(fchmodat(dirfd, "source.c", 0644, 0) == 0, "make source permissions unsafe");
    CHECK(phase2_workspace_open(host, "source.c", NULL, geteuid(), 256U * 1024U, &sourcefd, &artifactfd) < 0,
          "reject nonprivate source");
    CHECK(unlinkat(dirfd, "source.c", 0) == 0 && symlinkat("/etc/passwd", dirfd, "source.c") == 0,
          "replace source with a symlink fixture");
    CHECK(phase2_workspace_open(host, "source.c", NULL, geteuid(), 256U * 1024U, &sourcefd, &artifactfd) < 0,
          "reject symlink source instead of following it");
    CHECK(close(dirfd) == 0 && cleanup_dir(host, "source.c", NULL) == 0, "tear down validation workspace");
    return 0;
}

int main(void) {
    if (test_handoff() < 0 || test_rejections() < 0) return 1;
    puts("workspace handoff tests: bounded source, mapped permissions, artifact persistence, path rejection, teardown PASS");
    return 0;
}
