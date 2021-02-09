/*
 * bstr.c -- binary-stream routines
 */
#include "bstr.h"
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

bstr_t *
bpage_init(bpage_t *page)
{
    assert(page);
    // FIXME: need to enforce 4k page alignment...
    bstr_t *bstr = &page->pt.meta;
    bstr->head = ((octet_t *) &page->pt.head);
    bstr->base = page->pt.data;
    bstr->start = page->pt.data;
    bstr->content = page->pt.data;
    bstr->cursor = page->pt.data;
    bstr->end = page->pt.data;
    bstr->limit = page->pt.tail;
    bstr->tail = ((octet_t *) &page->pt.meta);
    assert(page->page == bstr->head);
    return bstr;
}

bstr_t *
bstr_alloc()
{
    bpage_t *page = calloc(1, sizeof(bpage_t));
    if (!page) return 0;
    return bpage_init(page);
}

bstr_t *
bstr_free(bstr_t *bstr)
{
    free(bstr->head);
    return 0;
}

int
bstr_put_raw(bstr_t *bstr, octet_t b)
{
    if (bstr->end >= bstr->limit) return -1;
    DEBUG(printf("bstr_put_raw: *%p = 0x%02x\n", bstr->end, b));
    *bstr->end++ = b;
    return 1;
}

int
bstr_put_int(bstr_t *bstr, int i)
{
    size_t n = encode_int(bstr->end, (bstr->limit - bstr->end), i);
    if (n == 0) return -1;
    bstr->end += n;
    return n;
}

int
bstr_put_int16(bstr_t *bstr, int16_t i)
{
    size_t n = 2 + sizeof(i);
    if (bstr->end + n > bstr->limit) return -1;
    octet_t b = (i < 0) ? m_int_0 : p_int_0;  // +/- Int, pad = 0
    if (bstr_put_raw(bstr, b) < 0) return -1;
    if (bstr_put_raw(bstr, n_2) < 0) return -1;  // size = 2
    b = i;  // lsb
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 8;  // msb
    if (bstr_put_raw(bstr, b) < 0) return -1;
    return n;
}

int
bstr_put_int32(bstr_t *bstr, int32_t i)
{
    size_t n = 2 + sizeof(i);
    if (bstr->end + n > bstr->limit) return -1;
    octet_t b = (i < 0) ? m_int_0 : p_int_0;  // +/- Int, pad = 0
    if (bstr_put_raw(bstr, b) < 0) return -1;
    if (bstr_put_raw(bstr, n_4) < 0) return -1;  // size = 2
    b = i;  // lsb
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 8;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 16;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 24;  // msb
    if (bstr_put_raw(bstr, b) < 0) return -1;
    return n;
}

int
bstr_put_int64(bstr_t *bstr, int64_t i)
{
    size_t n = 2 + sizeof(i);
    if (bstr->end + n > bstr->limit) return -1;
    octet_t b = (i < 0) ? m_int_0 : p_int_0;  // +/- Int, pad = 0
    if (bstr_put_raw(bstr, b) < 0) return -1;
    if (bstr_put_raw(bstr, n_8) < 0) return -1;  // size = 2
    b = i;  // lsb
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 8;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 16;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 24;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 32;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 40;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 48;
    if (bstr_put_raw(bstr, b) < 0) return -1;
    b = i >> 56;  // msb
    if (bstr_put_raw(bstr, b) < 0) return -1;
    return n;
}

int
bstr_put_blob(bstr_t *bstr, void *data, size_t size)
{
    size_t n = encode_blob(bstr->end, (bstr->limit - bstr->end), data, size);
    DEBUG(printf("bstr_put_blob: encoded %zu bytes\n", n));
    if (n == 0) return -1;
    bstr->end += n;
    return n;
}

int
bstr_open_array(bstr_t *bstr)
{
    DEBUG(printf("> bstr_open_array\n"));
    bstr->start = bstr->end;
    if (bstr_put_raw(bstr, array) < 0) return -1;
    if (bstr_put_int(bstr, (bstr->limit - bstr->end)) < 0) return -1;
    bstr->content = bstr->end;
    bstr->cursor = bstr->end;
    DEBUG(printf("< bstr_open_array %tu\n", bstr->end - bstr->start));
    return (bstr->end - bstr->start);  // number of bytes written to buffer
}

int
bstr_open_array_n(bstr_t *bstr, size_t n)
{
    DEBUG(printf("> bstr_open_array_n %zu\n", n));
    bstr->start = bstr->end;
    if (bstr_put_raw(bstr, array_n) < 0) return -1;
    if (bstr_put_int(bstr, (bstr->limit - bstr->end)) < 0) return -1;
    bstr->content = bstr->end;
    if (bstr_put_int(bstr, n) < 0) return -1;
    bstr->cursor = bstr->end;
    DEBUG(printf("< bstr_open_array_n %tu\n", bstr->end - bstr->start));
    return (bstr->end - bstr->start);  // number of bytes written to buffer
}

int
bstr_close_array(bstr_t *bstr)
{
    DEBUG(printf("> bstr_close_array\n"));
    octet_t *bp = bstr->start + 1;
    DEBUG(hexdump(stdout, bstr->start, (bstr->end - bstr->start)));
    DEBUG(printf("start=%p bp=%p content=%p cursor=%p end=%p limit=%p\n",
        bstr->start, bp, bstr->content, bstr->cursor, bstr->end, bstr->limit));
    size_t m = bstr->end - bstr->content;
    size_t w = bstr->content - bp;
    size_t n = encode_int_fixed(bp, w, m);
    if (n == 0) return -1;
    DEBUG(printf("< bstr_close_array %tu\n", bstr->end - bstr->start));
    return (bstr->end - bstr->start);  // number of bytes written to buffer
}

int
bstr_get_value(bstr_t *bstr)
{
    octet_t b;
    size_t n, m;

    if (!bstr) return -1;  // uninitialized bstr_t
    if (!bstr->end) return -1;  // uninitialized end
    if (!bstr->limit) return -1;  // uninitialized limit
    if (bstr->end >= bstr->limit) return -1;  // out-of-range
    bstr->start = bstr->end;
    b = *bstr->end++;
    DEBUG(printf("bstr_get_value: *%p = 0x%02x\n", bstr->start, b));
    DEBUG(hexdump(stdout, bstr->start, (bstr->limit - bstr->start)));
    if ((b >= array) && (b <= m_base_7) && (b != string_0)) {
        if (b == mem_ref) {
            // get "memo #" field

            if (bstr->end >= bstr->limit) return -1;
            bstr->content = bstr->end;
            b = *bstr->end++;
            bpage_t *page = (bpage_t *)bstr->head;
            if (b <= page->pt.head.memo_idx) {
                // resolve memo reference...
                bstr->cursor = bstr->base + page->pt.head.memo_ofs[b];
            }
        } else {
            // get "size" field
            int size;

            m = bstr->limit - bstr->end;
            n = decode_int(bstr->end, m, &size);
            if (n <= 0) return -1;
            DEBUG(printf("bstr_get_value: n=%zu size=%d\n", n, size));
            bstr->end += n;
            bstr->content = bstr->end;
            bstr->cursor = bstr->content;
            bstr->end += size;
            if (bstr->end > bstr->limit) return -1;
            if ((b == array_n) || (b == object_n)) {
                // get "count" field
                int count;

                m = bstr->end - bstr->content;
                n = decode_int(bstr->content, m, &count);
                if (n <= 0) return -1;
                DEBUG(printf("bstr_get_value: n=%zu size=%d\n", n, count));
                bstr->cursor += n;
                // FIXME: where should we store the "count" value?
            } else if (b == s_encoded) {
                // get "name" field

                // FIXME: handle arbitrary string encoding
            }
        }
    }
    m = bstr->end - bstr->start;
    DEBUG(hexdump(stdout, bstr->start, m));
    DEBUG(printf(
        "bstr_get_value: m=%zu start=%p content=%p cursor=%p end=%p limit=%p\n",
        m, bstr->start, bstr->content, bstr->cursor, bstr->end, bstr->limit));
    return m;  // number of bytes read from buffer
}

#ifdef TEST_MAIN
#include <stdio.h>
#include <assert.h>

static void
test_3_int_array(octet_t *buffer, size_t limit)
{
    size_t offset = 0;
    octet_t b;
    size_t n;
    int i, j, k;
    octet_t *data;

    DEBUG(printf("test_3_int_array: buffer=%p limit=%zu\n", buffer, limit));
    bstr_t meta = {
        .end = buffer,
        .limit = (buffer + limit),
    };
    i = bstr_get_value(&meta);
    DEBUG(printf("bstr_get_value() -> %d\n", i));

    assert(offset < limit);
    b = buffer[offset++];
    DEBUG(printf("b = 0x%02x\n", b));
    j = -1;  // unknown number of elements
    if (b == array) {

        // array size (in bytes)
        assert(offset < limit);
        n = decode_int(buffer + offset, limit - offset, &i);
        assert(n > 0);
        DEBUG(printf("i = %d\n", i));
        offset += n;
        assert(i == 3);

    } else if (b == array_n) {

        // array size (in bytes)
        assert(offset < limit);
        n = decode_int(buffer + offset, limit - offset, &i);
        assert(n > 0);
        offset += n;
        DEBUG(printf("i = %d\n", i));
        assert(i == 4);

        // array count (in elements)
        assert(offset < limit);
        n = decode_int(buffer + offset, limit - offset, &j);
        assert(n > 0);
        DEBUG(printf("j = %d\n", j));
        offset += n;
        i -= n;
        assert(j == 3);

    } else {

        // empty array
        assert(b == array_0);
        i = 0;

    }

    DEBUG(printf("offset=%zu i=%d j=%d limit=%zu\n", offset, i, j, limit));
    assert((offset + i) <= limit);
    data = buffer + offset;  // array data
    j = 0;  // offset into array data

    // array element 0
    assert(j < i);
    n = decode_int(data + j, i - j, &k);
    assert(n > 0);
    j += n;
    assert(k == SMOL_MIN);

    // array element 1
    assert(j < i);
    n = decode_int(data + j, i - j, &k);
    assert(n > 0);
    j += n;
    assert(k == 0);

    // array element 2
    assert(j < i);
    n = decode_int(data + j, i - j, &k);
    assert(n > 0);
    j += n;
    assert(k == SMOL_MAX);

    // array past end
    n = decode_int(data + j, i - j, &k);
    assert(n == 0);
    assert((offset + j) == limit);
}

void
test_bstr()
{
    octet_t buf_0[] = { array_n, n_4, n_3, n_m64, n_0, n_126 };
    octet_t buf_1[] = { array, n_3, n_m64, n_0, n_126 };

    test_3_int_array(buf_0, sizeof(buf_0));
    test_3_int_array(buf_1, sizeof(buf_1));

    static bpage_t bpage;
    assert(sizeof(bpage.page) == sizeof(bpage.pt));
    bstr_t *bstr = bpage_init(&bpage);
    DEBUG(printf("page (%p): \n", &bpage));
    DEBUG(printf("    head=%p base=%p start=%p content=%p\n",
        bstr->head, bstr->base, bstr->start, bstr->content));
    DEBUG(printf("    cursor=%p end=%p limit=%p tail=%p\n",
        bstr->cursor, bstr->end, bstr->limit, bstr->tail));

    bstr = bstr_alloc();
    assert(bstr);
//    bpage_t *page = (bpage_t *)bstr->head;
//    assert(sizeof(page->page) == sizeof(page->pt));
    DEBUG(printf("bstr (%p): \n", bstr));
    DEBUG(printf("    head=%p base=%p start=%p content=%p\n",
        bstr->head, bstr->base, bstr->start, bstr->content));
    DEBUG(printf("    cursor=%p end=%p limit=%p tail=%p\n",
        bstr->cursor, bstr->end, bstr->limit, bstr->tail));
    bstr_free(bstr);
}

int
main()
{
    test_bstr();
    return 0;  // success!
}
#endif /* TEST_MAIN */
