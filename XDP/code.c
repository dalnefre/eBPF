/*
 * code.c -- data encoding/decoding
 *
 * NOTE: This file is meant to be #include'd, not separately compiled.
 *       With -DTEST_MAIN, it builds a standalone unit-test program.
 */
#include "code.h"

static int u64_to_bytes(__u8 *data, __u8 *end, __u64 num)
{
    if (data + 8 > end) return 0;
    data[0] = num;
    num >>= 8;
    data[1] = num;
    num >>= 8;
    data[2] = num;
    num >>= 8;
    data[3] = num;
    num >>= 8;
    data[4] = num;
    num >>= 8;
    data[5] = num;
    num >>= 8;
    data[6] = num;
    num >>= 8;
    data[7] = num;
    return 8;
}

static int bytes_to_u64(__u8 *data, __u8 *end, __u64 *ptr)
{
    __u64 num = 0;

    if (data + 8 > end) return 0;
    num |= data[7];
    num <<= 8;
    num |= data[6];
    num <<= 8;
    num |= data[5];
    num <<= 8;
    num |= data[4];
    num <<= 8;
    num |= data[3];
    num <<= 8;
    num |= data[2];
    num <<= 8;
    num |= data[1];
    num <<= 8;
    num |= data[0];
    *ptr = num;
    return 8;
}

static int parse_int16(__u8 *data, __u8 *end, __s16 *ptr)
{
    __s16 num;

    if (data + 4 > end) return 0;  // out of bounds
    switch (data[0]) {
        case p_int_0:   num = 0;   break;
        case m_int_0:   num = -1;  break;
        default:        return 0;  // require +/- Int pad=0
    }
    if (data[1] != n_2) return 0;  // require size=2
    num = (num << 16) | (data[3] << 8) | data[2];
    *ptr = num;
    return 4;
}

static int code_int16(__u8 *data, __u8 *end, __s16 num)
{
    if (data + 4 > end) return 0;  // out of bounds
    data[0] = (num < 0) ? m_int_0 : p_int_0;  // +/- Int, pad = 0
    data[1] = n_2;  // size = 2
    data[2] = num;  // lsb
    data[3] = num >> 8;  // msb
    return 4;
}


#ifdef TEST_MAIN
#include <stdlib.h>
#include <stdio.h>
//#include <limits.h>
#include <assert.h>

void
test_int()
{
    __u8 buf[32];
    __u8 buf_0[] = { p_int_0, n_2, 0x00, 0x00 };
    __u8 buf_1[] = { p_int_0, n_2, 0x01, 0x00 };
    __u8 buf_2[] = { p_int_0, n_2, 0xFF, 0x7F };
    __u8 buf_3[] = { m_int_0, n_2, 0xFF, 0xFF };
#if 0
    __u8 buf_4[] = { p_int_0, n_2, 0xFE, 0xFF };
    __u8 buf_5[] = { m_int_0, n_2, 0xFE, 0xFF };
//    4275878552 = 0xFEDCBA98
    __u8 buf_7[] = { p_int_0, n_4, 0x98, 0xBA, 0xDC, 0xFE };
//    1147797409030816545 = 0xFEDCBA9876543210
    __u8 buf_8[] = { p_int_0, n_8,
//        0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE };
    1985229328 = 0x76543210
//    __u8 buf_9[] = { p_int_0, n_4, 0x10, 0x32, 0x54, 0x76 };
#endif
    __u8 buf_11[] = { octets, n_16,
        0x0, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7,
        0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF,
        0xF, 0xE, 0xD, 0xC, 0xB, 0xA, 0x9, 0x8,
        0x7, 0x6, 0x5, 0x4, 0x3, 0x2, 0x1, 0x0 };
    __s16 s16;
    __u64 u64;
    int len;

    s16 = 0xDEAD;
    len = parse_int16(buf_0, buf_0 + sizeof(buf_0), &s16);
    printf("buf_0: len=%d s16=%d (0x%x)\n", len, (int)s16, (int)s16);
    assert(len == 4);
    assert(s16 == 0);

    s16 = 0xDEAD;
    len = parse_int16(buf_1, buf_1 + sizeof(buf_1), &s16);
    printf("buf_1: len=%d s16=%d (0x%x)\n", len, (int)s16, (int)s16);
    assert(len == 4);
    assert(s16 == 1);

    s16 = 0xDEAD;
    len = parse_int16(buf_2, buf_2 + sizeof(buf_2), &s16);
    printf("buf_2: len=%d s16=%d (0x%x)\n", len, (int)s16, (int)s16);
    assert(len == 4);
    assert(s16 == 32767);

    s16 = 0xDEAD;
    len = parse_int16(buf_3, buf_3 + sizeof(buf_3), &s16);
    printf("buf_3: len=%d s16=%d (0x%x)\n", len, (int)s16, (int)s16);
    assert(len == 4);
    assert(s16 == -1);

    len = code_int16(buf, buf + sizeof(buf), -12345);
    assert(len == 4);
    s16 = 0xDEAD;
    len = parse_int16(buf, buf + sizeof(buf), &s16);
    printf("buf: len=%d s16=%d (0x%x)\n", len, (int)s16, (int)s16);
    assert(len == 4);
    assert(s16 == -12345);

    len = bytes_to_u64(buf_11 + 2, buf_11 + 10, &u64);
    assert(len == sizeof(u64));
    u64_to_bytes(buf, buf + sizeof(buf), u64);
}

int
main()
{
    test_int();
    exit(EXIT_SUCCESS);
}
#endif /* TEST_MAIN */
