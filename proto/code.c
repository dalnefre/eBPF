/*
 * code.c -- data encoding/decoding
 */
#include "code.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <assert.h>

#define DEBUG(x)   /**/
#if (0 DEBUG(+1))
#include <stdio.h>
#include <inttypes.h>
#include "util.h"
#endif

/*
 * encoding routines
 */

size_t
encode_int(BYTE *buffer, size_t limit, int data)
{
    size_t offset = 0;
    size_t content;  // offset to variable-length content
    BYTE b;

    if ((SMOL_MIN <= data) && (data <= SMOL_MAX)) {
        // "smol" integer
        if (offset >= limit) return 0;  // out-of-bounds
        b = INT2SMOL(data);
        DEBUG(printf("encode_smol: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
        return offset;  // number of bytes written to buffer
    }

    // signed integer (extended)
    if ((offset + 1) >= limit) return 0;  // out-of-bounds
    if (data < 0) {
        b = m_int_0;  // minus int no-pad
        DEBUG(printf("encode_int: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
        content = ++offset;  // reserved space for "size" field
        while (data != -1) {
            if (offset >= limit) return 0;  // out-of-bounds
            b = (data & 0xFF);  // grab LSB
            DEBUG(printf("encode_int: %p[%zu] = 0x%02x\n", buffer, offset, b));
            buffer[offset++] = b;
            data >>= 8;  // remove LSB
        }
    } else {
        b = p_int_0;  // plus int no-pad
        DEBUG(printf("encode_int: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
        content = ++offset;  // reserved space for "size" field
        while (data != 0) {
            if (offset >= limit) return 0;  // out-of-bounds
            b = (data & 0xFF);  // grab LSB
            DEBUG(printf("encode_int: %p[%zu] = 0x%02x\n", buffer, offset, b));
            buffer[offset++] = b;
            data >>= 8;  // remove LSB
        }
    }
    b = INT2SMOL(offset - content);
    DEBUG(printf("encode_int: %p[%zu] = 0x%02x\n", buffer, content - 1, b));
    buffer[content - 1] = b;  // update "size" field

    return offset;  // number of bytes written to buffer
}

size_t
encode_int_fixed(BYTE *buffer, size_t width, int data)
{
    size_t offset = 0;
    BYTE b, s;

    if (width < 1) return 0;  // not enough space
    if (width == 1) {
        if ((SMOL_MIN > data) && (data > SMOL_MAX)) return 0;  // out-of-range
        // "smol" integer
        b = INT2SMOL(data);
        DEBUG(printf("encode_int_fixed: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
        return offset;  // number of bytes written to buffer
    }

    // signed integer (extended)
    if (width < 3) return 0;  // not enough space
    if (width > SMOL_MAX) return 0;  // too much space

    s = (data < 0) ? -1 : 0;  // sign-extension
    b = (data < 0) ? m_int_0 : p_int_0;  // signed int, no padding
    DEBUG(printf("encode_int_fixed: %p[%zu] = 0x%02x\n", buffer, offset, b));
    buffer[offset++] = b;

    b = INT2SMOL(width - 2);  // "size" field
    DEBUG(printf("encode_int_fixed: %p[%zu] = 0x%02x\n", buffer, offset, b));
    buffer[offset++] = b;

    while (offset < width) {
        b = (data & 0xFF);  // grab LSB
        DEBUG(printf("encode_int_fixed: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
        data >>= 8;  // remove LSB
    }

    if (data != s) return 0;  // significant bits left over

    return offset;  // number of bytes written to buffer
}

size_t
encode_cstr(BYTE *buffer, size_t limit, char *s)
{
    size_t offset = 0;
    BYTE b;
    size_t n;

    size_t len = strlen(s);
    if (offset >= limit) return 0;  // out-of-bounds
    if (len == 0) {  // empty string
        b = string_0;
        DEBUG(printf("encode_cstr: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
    } else {
        b = utf8;  // utf8 string
        DEBUG(printf("encode_cstr: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
        // string length in bytes
        n = encode_int(buffer + offset, limit - offset, len);
        if (n <= 0) return 0;  // error
        offset += n;
        // string content (one byte per character, ascii only)
        if ((offset + n) >= limit) return 0;  // out-of-bounds
        while (len-- > 0) {
            b = *s++;
            if (b & 0x80) {
                b = 0x1a;  // SUB for non-ascii
            }
            DEBUG(printf("encode_cstr: %p[%zu] = 0x%02x\n", buffer, offset, b));
            buffer[offset++] = b;
        }
    }
    return offset;  // number of bytes written to buffer
}

size_t
encode_blob(BYTE *buffer, size_t limit, void *data, size_t size)
{
    size_t offset = 0;
    BYTE b;
    size_t n;

    if (offset >= limit) return 0;  // out-of-bounds
    b = octets;  // raw data bytes
    DEBUG(printf("encode_blob: %p[%zu] = 0x%02x\n", buffer, offset, b));
    buffer[offset++] = b;

    // data length in bytes
    n = encode_int(buffer + offset, limit - offset, size);
    if (n <= 0) return 0;  // error
    offset += n;

    // blob content (data bytes)
    if ((offset + size) >= limit) return 0;  // out-of-bounds
    BYTE *blob = data;
    while (size-- > 0) {
        b = *blob++;
        DEBUG(printf("encode_blob: %p[%zu] = 0x%02x\n", buffer, offset, b));
        buffer[offset++] = b;
    }

    return offset;  // number of bytes written to buffer
}

size_t
encode_array_of_int(BYTE *buffer, size_t limit, int *data, size_t count)
{
#if 0
    bstr_t meta = {
        .end = buffer,
        .limit = (buffer + limit),
    };

    if (bstr_open_array_n(&meta, count) < 0) return 0;
    while (count--) {
        if (bstr_put_int(&meta, *data++) < 0) return 0;
    }
    if (bstr_close_array(&meta) < 0) return 0;
    return (meta.end - meta.start);  // number of bytes written to buffer
#else
    size_t offset = 0;
    size_t content;  // offset to variable-length content
    size_t n, m;
    BYTE b;

    // enforce argument pre-conditions
    if (limit > (size_t)INT_MAX) return 0;  // limit too large
    if (count > (size_t)INT_MAX) return 0;  // count too large

    // data type: array[n]
    if (offset >= limit) return 0;  // out-of-bounds
    b = array_n;  // counted array
    DEBUG(printf("encode_array_of_int: %p[%zu] = 0x%02x\n", buffer, offset, b));
    buffer[offset++] = b;

    // array size (bytes)
    m = limit - offset;
    n = encode_int(buffer + offset, m, (int)m - 1);
    DEBUG(printf("encode_array_of_int: %zu bytes for size limit %zu\n", n, m));
    if (n <= 0) return 0;  // error
    offset += n;
    content = offset;
    m = n;  // remember number of bytes available to encode array size

    // array count (elements)
    n = encode_int(buffer + offset, limit - offset, (int)count);
    if (n <= 0) return 0;  // error
    offset += n;

    // array elements
    while (count-- > 0) {
        n = encode_int(buffer + offset, limit - offset, *data++);
        if (n <= 0) return 0;  // error
        offset += n;
    }

    // update array size
    if (offset > (size_t)INT_MAX) return 0;  // offset too large
    n = encode_int_fixed(buffer + 1, m, (int)offset - content);
    DEBUG(printf("encode_array_of_int: size used %zu of %zu bytes\n", n, m));
    if (n != m) return 0;  // encoding size mismatch

    DEBUG(printf("encode_array_of_int: final offset = %zu\n", offset));
    return offset;  // number of bytes written to buffer
#endif
}

/*
 * decoding routines
 */

size_t
decode_int(BYTE *buffer, size_t limit, int *data)
{
    size_t offset = 0;
    BYTE b;

    DEBUG(printf("decode_int: buffer=%p limit=%zu data=%p\n",
        buffer, limit, data));
    if (offset >= limit) return 0;  // out-of-bounds
    b = buffer[offset++];
    DEBUG(printf("decode_int: b=0x%02x\n", b));
    if ((n_m64 <= b) && (b <= n_126)) {
        // "smol" integer
        int i = SMOL2INT(b);
        DEBUG(printf("decode_smol: %d\n", i));
        *data = i;
        return offset;  // number of bytes read from buffer
    }
    if ((b & 0xF0) == 0x10) {
        // signed integer (extended)
        int i = ((b & 0x08) ? -1 : 0);  // sign-extend
        int j;
        size_t n = decode_int(buffer + offset, limit - offset, &j);
        if (n <= 0) return 0;  // error
        offset += n;
        if (j > sizeof(i)) return 0;  // too big!
        offset += j;
        if (offset > limit) return 0;  // out-of-bounds
        for (int k = 1; k <= j; ++k) {
            b = buffer[offset - k];
            i <<= 8;  // make room for MSB
            i |= b;
        }
        DEBUG(printf("decode_int: %d\n", i));
        *data = i;
        return offset;  // number of bytes read from buffer
    }

    return offset;  // number of bytes read from buffer
}

size_t
decode_int64(BYTE *buffer, size_t limit, int64_t *data)
{
    size_t offset = 0;
    BYTE b;

    DEBUG(printf("decode_int64: buffer=%p limit=%zu data=%p\n",
        buffer, limit, data));
    if (offset >= limit) return 0;  // out-of-bounds
    b = buffer[offset++];
    DEBUG(printf("decode_int64: b=0x%02x\n", b));
    if ((n_m64 <= b) && (b <= n_126)) {
        // "smol" integer
        int64_t i = SMOL2INT(b);
        DEBUG(printf("decode_smol64: %" PRId64 "\n", i));
        *data = i;
        return offset;  // number of bytes read from buffer
    }
    if ((b & 0xF0) == 0x10) {
        // signed integer (extended)
        int64_t i = ((b & 0x08) ? -1 : 0);  // sign-extend
        int j;
        size_t n = decode_int(buffer + offset, limit - offset, &j);
        if (n <= 0) return 0;  // error
        offset += n;
        if (j > sizeof(i)) return 0;  // too big!
        offset += j;
        if (offset > limit) return 0;  // out-of-bounds
        for (int k = 1; k <= j; ++k) {
            b = buffer[offset - k];
            i <<= 8;  // make room for MSB
            i |= b;
        }
        DEBUG(printf("decode_int64: %" PRId64 "\n", i));
        *data = i;
        return offset;  // number of bytes read from buffer
    }

    return offset;  // number of bytes read from buffer
}

size_t
decode_cstr(BYTE *buffer, size_t limit, char *data, size_t size)
{
    size_t offset = 0;
    BYTE b;
    int i, j;

    DEBUG(printf("decode_cstr: buffer=%p limit=%zu data=%p size=%zu\n",
        buffer, limit, data, size));
    if (offset >= limit) return 0;  // out-of-bounds
    b = buffer[offset++];
    DEBUG(printf("decode_cstr: b=0x%02x\n", b));
    if ((b == octets) || (b == utf8)) {
        size_t n = decode_int(buffer + offset, limit - offset, &j);
        if (n <= 0) return 0;  // error
        offset += n;
        if (offset + j > limit) return 0;  // out-of-bounds
        for (i = 0; i < j; ++i) {
            b = buffer[offset];
            DEBUG(printf("decode_cstr: %p[%zu] = 0x%02x\n", buffer, offset, b));
            ++offset;
            if (b & 0x80) {
                b = 0x1a;  // SUB for non-ascii
            }
            if (i < size) {
                data[i] = b;
            }
        }
    } else if (b == string_0) {
        i = 0;
    } else {
        return 0;  // upsupported string type
    }
    if (i >= size) {
        i = (int)size - 1;  // truncate output
    }
    if (i >= 0) {
        data[i] = '\0';  // NUL terminate string
    }
    DEBUG(printf("decode_cstr: \"%s\"\n", data));
    return offset;  // number of bytes read from buffer
}

#ifdef TEST_MAIN
#include <stdio.h>
#include <assert.h>

static void
assert_buf(BYTE *expect, size_t size, BYTE *buf, size_t n)
{
    DEBUG(hexdump(stdout, expect, size));
//    DEBUG(hexdump(stdout, buf, n));
    assert(size == n);
    for (size_t i = 0; i < n; ++i) {
        BYTE a = expect[i];
        BYTE b = buf[i];
        if (a != b) {
            fprintf(stderr,
                "mismatch at %p[%zu], expect 0x%02x, actual 0x%02x\n",
                buf, i, a, b);
        }
        assert(a == b);
    }
}

void
test_encode()
{
    BYTE buf[18];
    size_t n, m;
    int i;

    int data_array[] = { SMOL_MIN, 0, SMOL_MAX };
    m = sizeof(data_array) / sizeof(int);
    n = encode_array_of_int(buf, sizeof(buf), data_array, m);
    BYTE expect_buf[] = { array_n, n_4, n_3, n_m64, n_0, n_126 };
    assert_buf(expect_buf, sizeof(expect_buf), buf, n);

    m = sizeof(data_array) / sizeof(int);
    n = encode_array_of_int(buf, 4, data_array, m);
    assert(n == 0);  // out-of-bounds error expected

    i = 0x123456;
    n = encode_int(buf, sizeof(buf), i);
    assert(n > 0);
    m = encode_int(buf + n, sizeof(buf) - n, -i);
    assert(m > 0);
    n += m;
    BYTE expect_num[] = {
        p_int_0, n_3, 0x56, 0x34, 0x12,
        m_int_0, n_3, 0xaa, 0xcb, 0xed
    };
    assert_buf(expect_num, sizeof(expect_num), buf, n);

    char *cstr = "clich\xE9";
    n = encode_cstr(buf, sizeof(buf), cstr);
    BYTE expect_str[] = { utf8, n_6, 'c', 'l', 'i', 'c', 'h', 0x1A };
    assert_buf(expect_str, sizeof(expect_str), buf, n);

}

void
test_decode()
{
    char scratch[64];
    BYTE buf_2[] = {
        p_int_0, n_3, 0x56, 0x34, 0x12,
        m_int_0, n_3, 0xaa, 0xcb, 0xed
    };
    BYTE buf_3[] = { utf8, n_7, 'c', 'l', 'i', 'c', 'h', 0xC3, 0xA9 };
    BYTE *bp;
    BYTE b;
    size_t n;
    int i;

    bp = buf_2;
    n = decode_int(bp, sizeof(buf_2), &i);
    DEBUG(printf("n=%zu i=0x%x\n", n, i));
    assert(n == 5);
    assert(i == 0x123456);
    bp += n;
    n = decode_int(bp, sizeof(buf_2) - n, &i);
    DEBUG(printf("n=%zu i=0x%x\n", n, i));
    assert(n == 5);
    assert(i == -0x123456);

    n = decode_cstr(buf_3, sizeof(buf_3), scratch, sizeof(scratch));
    assert(n == 9);

    b = n_m1;
    i = SMOL2INT(b);
    n = SMOL2INT(b);
    DEBUG(printf("SMOL2INT(0x%02x) -> (int)%d (size_t)%zu (BYTE)%u\n",
        b, i, n, (BYTE)n));
    assert(SMOL2INT(b) == -1);
}

int
main()
{
    test_encode();
    test_decode();
    return 0;  // success!
}
#endif /* TEST_MAIN */
