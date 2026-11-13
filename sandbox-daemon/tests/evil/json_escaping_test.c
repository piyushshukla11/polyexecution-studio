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
        0xF0, 0x90, 0x80, '\n' /* Truncated 4-byte */
    };
    write(STDOUT_FILENO, s8, sizeof(s8));

    /* 9. Collision test cases: valid UTF-8 (C3 A9, C2 80, C3 BF) vs invalid bytes (E9, FF) */
    const unsigned char s9[] = {
        'C', 'o', 'l', 'l', 'i', 's', 'i', 'o', 'n', ':', ' ',
        0xC3, 0xA9, ' ', /* Valid UTF-8 C3 A9 (U+00E9) */
        0xC2, 0x80, ' ', /* Valid UTF-8 C2 80 (U+0080) */
        0xC3, 0xBF, ' ', /* Valid UTF-8 C3 BF (U+00BF) */
        0xE9, ' ',       /* Raw invalid byte E9 */
        0xFF, '\n'       /* Raw invalid byte FF */
    };
    write(STDOUT_FILENO, s9, sizeof(s9));

    /* 10. Stderr output with quotes, backslashes, control characters, valid UTF-8, and invalid bytes */
    const unsigned char err[] = {
        'S', 't', 'd', 'e', 'r', 'r', ':', ' ',
        '"', 'q', 'u', 'o', 't', 'e', 'd', '"', ' ',
        '\\', 's', 'l', 'a', 's', 'h', '\\', ' ',
        0x02, 0x03, ' ',
        0xC3, 0xA9, ' ',
        0xE9, ' ',
        0xFF, '\n'
    };
    write(STDERR_FILENO, err, sizeof(err));

    return 0;
}
