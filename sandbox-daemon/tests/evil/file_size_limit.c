#include <unistd.h>
#include <fcntl.h>

int main(void) {
    /* Open /tmp/pes_test_fsize.tmp and write 2 MiB to trigger SIGXFSZ under RLIMIT_FSIZE (1 MiB) */
    int fd = open("/tmp/pes_test_fsize.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return 1;
    char buf[4096];
    for (int i = 0; i < 4096; i++) buf[i] = 'F';
    for (int i = 0; i < 512; i++) {
        if (write(fd, buf, sizeof(buf)) < 0) {
            close(fd);
            return 2;
        }
    }
    close(fd);
    unlink("/tmp/pes_test_fsize.tmp");
    return 0;
}
