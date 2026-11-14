#include <unistd.h>
#include <fcntl.h>

int main(void) {
    /*
     * Phase 2 Adversarial Fixture: Host Filesystem Snooping
     * Attempt to read sensitive host files (/etc/passwd).
     * In Phase 2, this must be blocked by mount namespaces (CLONE_NEWNS)
     * with pivot_root and a minimal, unprivileged rootfs.
     * DO NOT run against Phase 1 runner!
     */
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd >= 0) {
        char buf[64];
        ssize_t n = read(fd, buf, sizeof(buf));
        close(fd);
        if (n > 0) {
            /* Breach: Host filesystem read succeeded */
            write(STDOUT_FILENO, "HOST_PASSWD_LEAKED\n", 19);
            return 0;
        }
    }
    /* Safe Phase 2 termination: open failed */
    return 1;
}
