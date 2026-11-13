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

    /* 4. Invalid lead bytes above 0xF4 (0xF5, 0xF7) */
    const unsigned char s4[] = {
        'L', 'e', 'a', 'd', '>', 'F', '4', ':', ' ',
        0xF5, 0x80, 0x80, 0x80, ' ',
        0xF7, 0x80, 0x80, 0x80, '\n'
    };
    write(STDOUT_FILENO, s4, sizeof(s4));

    /* 5. Overlong encodings (2-byte NUL/ASCII, 3-byte, 4-byte) */
    const unsigned char s5[] = {
        'O', 'v', 'e', 'r', 'l', 'o', 'n', 'g', ':', ' ',
        0xC0, 0x80, ' ',       /* Overlong 2-byte NUL */
        0xC1, 0xBF, ' ',       /* Overlong 2-byte ASCII 0x7F */
        0xE0, 0x80, 0x80, ' ', /* Overlong 3-byte */
        0xF0, 0x80, 0x80, 0x80, '\n' /* Overlong 4-byte */
    };
    write(STDOUT_FILENO, s5, sizeof(s5));

    /* 6. Surrogate encodings (U+D800..U+DFFF) and out-of-range (> U+10FFFF) */
    const unsigned char s6[] = {
        'S', 'u', 'r', 'r', 'o', 'g', 'a', 't', 'e', ':', ' ',
        0xED, 0xA0, 0x80, ' ', /* U+D800 */
        0xED, 0xBF, 0xBF, ' ', /* U+DFFF */
        0xF4, 0x90, 0x80, 0x80, '\n' /* U+110000 (> U+10FFFF) */
    };
    write(STDOUT_FILENO, s6, sizeof(s6));

    /* 7. Isolated continuation bytes */
    const unsigned char s7[] = {
        'I', 's', 'o', 'l', 'a', 't', 'e', 'd', ':', ' ',
        0x80, ' ', 0xBF, ' ', 0x90, '\n'
    };
    write(STDOUT_FILENO, s7, sizeof(s7));

    /* 8. Truncated multi-byte sequences at end */
    const unsigned char s8[] = {
        'T', 'r', 'u', 'n', 'c', ':', ' ',
        0xC2, ' ',             /* Truncated 2-byte */
        0xE4, 0xB8, ' ',       /* Truncated 3-byte */
        0xF0, 0x90, 0x80       /* Truncated 4-byte without newline */
    };
    write(STDOUT_FILENO, s8, sizeof(s8));

    /* 9. Stderr output with quotes, backslashes, control characters, and invalid bytes */
    const char err[] = "Stderr test: \"quoted\" and \\slash with ctrl: \x02\x03 and invalid: \xFF\xFE\n";
    write(STDERR_FILENO, err, sizeof(err) - 1);

    return 0;
}
