#include <unistd.h>
#include <fcntl.h>

int main(void) {
    /*
     * Phase 2 Adversarial Fixture: Host Filesystem Snooping
     * Attempt to open and read sensitive host files (/etc/passwd).
     * In Phase 2, this must be blocked by mount namespaces (CLONE_NEWNS)
     * with pivot_root into a minimal, unprivileged rootfs where /etc/passwd
     * does not exist, or by seccomp file open restrictions.
     * DO NOT run against Phase 1 runner!
     */
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        char buf[64];
        (void)read(fd, buf, sizeof(buf));
        close(fd);
        /* Breach: Host file /etc/passwd exists and was opened */
        write(STDOUT_FILENO, "HOST_PASSWD_LEAKED\n", 19);
        return 0;
    }
    /* Safe Phase 2 containment: open failed (e.g. ENOENT or EPERM/EACCES) */
    return 1;
}
