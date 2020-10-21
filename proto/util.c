/*
 * util.c -- utility routines
 */
#include "util.h"
#include <stdint.h>

void
hexdump(FILE *f, void *data, size_t size)
{
    uint8_t *buffer = data;
    const int span = 16;
    size_t offset = 0;
    int i, j;

    while (offset < size) {
        fprintf(f, "%04zx:  ", offset);
        for (i = 0; i < span; ++i) {
            if (i == 8) {
                fputc(' ', f);  // gutter between 64-bit words
            }
            j = offset + i;
            if (j < size) {
                fprintf(f, "%02x ", buffer[j]);
            } else {
                fputs("   ", f);
            }
        }
        fputc(' ', f);
        fputc('|', f);
        for (i = 0; i < span; ++i) {
            j = offset + i;
            if (j < size) {
                uint8_t b = buffer[j];
                if ((0x20 <= b) && (b < 0x7F)) {
                    fprintf(f, "%c", (int)b);
                } else {
                    fputc('.', f);
                }
            } else {
                fputc(' ', f);
            }
        }
        fputc('|', f);
        fputc('\n', f);
        offset += span;
    }
    fflush(f);
}

#ifdef TEST_MAIN
#include <assert.h>

void
test_util()
{
    printf("sizeof(short)=%zu sizeof(int)=%zu sizeof(long)=%zu\n",
        sizeof(short), sizeof(int), sizeof(long));
    printf("sizeof(size_t)=%zu sizeof(ptrdiff_t)=%zu sizeof(intptr_t)=%zu\n",
        sizeof(size_t), sizeof(ptrdiff_t), sizeof(intptr_t));
}

int
main()
{
    test_util();
    return 0;  // success!
}
#endif /* TEST_MAIN */
