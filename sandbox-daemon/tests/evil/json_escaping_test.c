#include <unistd.h>
#include <string.h>

int main(void) {
    /* 1. Quotes and backslashes */
    const char s1[] = "Quotes: \"hello\" and backslashes: \\path\\to\\file\n";
    write(STDOUT_FILENO, s1, sizeof(s1) - 1);

    /* 2. Control characters (0x00, 0x01, 0x08, 0x0C, 0x1F, 0x7F) */
    const char s2[] = { 'C', 't', 'r', 'l', ':', ' ', 0x00, 0x01, 0x08, 0x0C, 0x1F, 0x7F, '\n' };
    write(STDOUT_FILENO, s2, sizeof(s2));

    /* 3. Valid multi-byte UTF-8 (2-byte Greek, 3-byte Chinese, 4-byte Unicode U+10000) */
    const unsigned char s3[] = {
        'U', 'T', 'F', '-', '8', ':', ' ',
        /* 2-byte Greek alpha beta: \xCE\xB1\xCE\xB2 */
        0xCE, 0xB1, 0xCE, 0xB2, ' ',
        /* 3-byte Chinese: \xE4\xB8\x96\xE7\x95\x8C */
        0xE4, 0xB8, 0x96, 0xE7, 0x95, 0x8C, ' ',
        /* 4-byte Unicode U+10000: \xF0\x90\x80\x80 */
        0xF0, 0x90, 0x80, 0x80, '\n'
    };
    write(STDOUT_FILENO, s3, sizeof(s3));

    /* 4. Invalid raw byte sequences (0xFF, 0xFE, 0x80, 0xC0) */
    const unsigned char s4[] = { 'I', 'n', 'v', 'a', 'l', 'i', 'd', ':', ' ', 0xFF, 0xFE, 0x80, 0xC0, '\n' };
    write(STDOUT_FILENO, s4, sizeof(s4));

    /* 5. Stderr output with quotes, backslashes, and control characters */
    const char err[] = "Stderr test: \"quoted\" and \\slash with ctrl: \x02\x03\n";
    write(STDERR_FILENO, err, sizeof(err) - 1);

    return 0;
}
