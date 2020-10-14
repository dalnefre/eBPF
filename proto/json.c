/*
 * json.c -- JSON data-types
 */
#include "json.h"
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

int
json_get_value(json_t *json)
{
    int n;
    BYTE b;

    if (!json) return -1;  // uninitialized json_t
    bstr_t *bstr = json->bstr;
    if (!bstr) return -1;  // uninitialized bstr_t
    n = bstr->limit - bstr->end;
    DEBUG(printf("json_get_value: end=%p limit=%p n=%d\n",
        bstr->end, bstr->limit, n));
    DEBUG(hexdump(stdout, bstr->end, n));
    n = bstr_get_value(bstr);
    if (n < 0) return -1;  // parse failed
    DEBUG(printf("json_get_value: n=%d start=%p content=%p end=%p\n",
        n, bstr->start, bstr->content, bstr->end));
    json->index = 0;  // default "index"
    json->count = -1;  // unknown "count"
    b = json->val.raw = *bstr->start;  // raw prefix octet
    if (b == null) {
        json->type = JSON_Null;
        DEBUG(printf("json_get_value: json->type = JSON_Null\n"));
    } else if ((b == true) || (b == false)) {
        json->type = JSON_Boolean;
        DEBUG(printf("json_get_value: json->type = JSON_Boolean\n"));
    } else if ((b & 0xF8) == 0x08) {
        json->type = JSON_String;
        DEBUG(printf("json_get_value: json->type = JSON_String\n"));
        if (b == string_0) {
            json->val.str.enc = ENC_Raw;
            json->count = 0;  // empty string
        } else if (b == octets) {
            json->val.str.enc = ENC_Raw;
            json->count = bstr->end - bstr->cursor;
        } else if (b == utf8) {
            json->val.str.enc = ENC_UTF_8;
        } else {
            return -1;  // unsupported encoding
        }
    } else if ((b & 0xF9) == 0x00) {
        json->type = JSON_Array;
        DEBUG(printf("json_get_value: json->type = JSON_Array\n"));
        if (b == array_0) {
            json->count = 0;  // empty array
        } else if (b == array_n) {
            size_t z = bstr->cursor - bstr->content;
            z = decode_int(bstr->content, z, &json->count);
            if (z <= 0) return -1;  // bad "count" field
        }
    } else if ((b & 0xF9) == 0x01) {
        json->type = JSON_Object;
        DEBUG(printf("json_get_value: json->type = JSON_Object\n"));
        if (b == object_0) {
            json->count = 0;  // empty object
        } else if (b == object_n) {
            size_t z = bstr->cursor - bstr->content;
            z = decode_int(bstr->content, z, &json->count);
            if (z <= 0) return -1;  // bad "count" field
        }
    } else {
        json->type = JSON_Number;
        DEBUG(printf("json_get_value: json->type = JSON_Number\n"));
        json->val.num.base = 10;  // default base
        json->val.num.exp = 0;  // default exponent
        if ((n_m64 <= b) && (b <= n_126)) {
            json->count = (b < n_0) ? 6 : 7;  // bit "count"
            json->val.num.bits = SMOL2INT(b);
        } else {
            int64_t s = (b & 0x08) ? -1 : 0;  // sign extension
            int64_t i = s;
            size_t z = bstr->end - bstr->cursor;
            if (b & 0x20) {
                // extra number field
                z = decode_int64(bstr->cursor, z, &i);
                if (z <= 0) return -1;  // error
                bstr->cursor += z;
                if (b & 0x10) {
                    // another number field
                    json->val.num.base = i;
                    z = bstr->end - bstr->cursor;
                    z = decode_int64(bstr->cursor, z, &i);
                    if (z <= 0) return -1;  // error
                    bstr->cursor += z;
                }
                json->val.num.exp = i;
                z = bstr->end - bstr->cursor;
            }
            json->count = z << 3;  // bit "count"
            json->count -= (b & 0x7);  // minus padding
            if (json->count > 64) {
                json->val.num.bits = s;
                return n;  // early exit for bignums...
            }
            // scan "int" bits
            i = s;
            BYTE *bp = bstr->cursor;
            while (z-- > 0) {
                i = (i << 8) | bp[z];
            }
            json->val.num.bits = i;
        }
    }
    return n;
}

int
json_get_int64(json_t *json)
{
    int rv = json_get_value(json);
    if (rv <= 0) return -1;  // error
    if (json->type != JSON_Number) return -1;  // error
    if (json->count > 64) return -1;  // error
    if (json->val.num.base != 10) return -1;  // error
    if (json->val.num.exp != 0) return -1;  // error
    // note: value in `json->val.num.bits`
    return rv;
}

#ifdef TEST_MAIN
#include <stdio.h>
#include <assert.h>

void
test_json()
{
    BYTE buf_0[] = { array_n, n_4, n_3, n_m64, n_0, n_126 };
    BYTE buf_1[] = { array, n_42,
        m_int_0, n_3, 0xFE, 0xFF, 0xFF,
        n_0,
        p_int_3, n_8, 0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10,
        m_int_5, n_7, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xFF,
        p_base_7, n_3, n_13, n_m1, 0x01,
        p_dec_0, n_10, n_m5, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    };
/*
{
    "space" : {
        "origin" : [ -40, -20 ],
        "extent" : [ 600, 460 ]
    },
    "shapes" : [
        {
            "origin" : [ 5, 3 ],
            "extent" : [ 21, 13 ]
        },
        {
            "origin" : [ 8, 5 ],
            "extent" : [ 13, 8 ]
        }
    ]
}
*/
    BYTE buf_2[] = { object_n, n_80, n_2,
        utf8, n_5, 's', 'p', 'a', 'c', 'e',
        object, n_32,
            utf8_mem, n_6, 'o', 'r', 'i', 'g', 'i', 'n',
            array_n, n_3, n_2,
                n_m40,
                n_m20,
            utf8_mem, n_6, 'e', 'x', 't', 'e', 'n', 't',
            array_n, n_9, n_2,
                p_int_0, 2, 600 & 0xFF, 600 >> 8,
                p_int_0, 2, 460 & 0xFF, 460 >> 8,
        utf8, n_6, 's', 'h', 'a', 'p', 'e', 's',
        array, n_28,
            object, n_12,
                mem_ref, 0, array, n_2, n_5, n_3,
                mem_ref, 1, array, n_2, n_21, n_13,
            object, n_12,
                mem_ref, 0, array, n_2, n_8, n_5,
                mem_ref, 1, array, n_2, n_13, n_8,
    };
    bstr_t bstr;
    json_t json = { .bstr = &bstr };
    bstr_t part;
    json_t item = { .bstr = &part };
    size_t n;

    bstr.end = buf_0;
    bstr.limit = buf_0 + sizeof(buf_0);
    n = json_get_value(&json);
    DEBUG(printf("buf_0: n=%zu start=%p content=%p cursor=%p end=%p limit=%p\n",
        n, bstr.start, bstr.content, bstr.cursor, bstr.end, bstr.limit)); 
    assert(n > 0);
    assert(json.type == JSON_Array);
    assert(json.count == 3);

    part.end = bstr.cursor;
    part.limit = bstr.end;
    while (part.end < part.limit) {
        n = json_get_value(&item);
        DEBUG(printf("part: n=%zu start=%p end=%p limit=%p\n",
            n, bstr.start, bstr.end, bstr.limit)); 
        assert(n > 0);
        assert(item.type == JSON_Number);
        DEBUG(printf("      count=%d"
            " int=%" PRId64 " base=%" PRId64 " exp=%" PRId64 "\n",
            item.count,
            item.val.num.bits, item.val.num.base, item.val.num.exp));
    }

    bstr.end = buf_1;
    bstr.limit = buf_1 + sizeof(buf_1);
    n = json_get_value(&json);
    DEBUG(printf("buf_1: n=%zu start=%p content=%p cursor=%p end=%p limit=%p\n",
        n, bstr.start, bstr.content, bstr.cursor, bstr.end, bstr.limit)); 
    assert(n > 0);
    assert(json.type == JSON_Array);
    assert(json.count == -1);

    part.end = bstr.cursor;
    part.limit = bstr.end;
    while (part.end < part.limit) {
        n = json_get_value(&item);
        DEBUG(printf("part: n=%zu start=%p end=%p limit=%p\n",
            n, bstr.start, bstr.end, bstr.limit)); 
        assert(n > 0);
        assert(item.type == JSON_Number);
        DEBUG(printf("      count=%d"
            " int=%" PRId64 " base=%" PRId64 " exp=%" PRId64 "\n",
            item.count,
            item.val.num.bits, item.val.num.base, item.val.num.exp));
        DEBUG(hexdump(stdout, &item.val.num.bits, sizeof(item.val.num.bits)));
    }

    DEBUG(hexdump(stdout, buf_2, sizeof(buf_2)));
    bstr.end = buf_2;
    bstr.limit = buf_2 + sizeof(buf_2);
    n = json_get_value(&json);
    DEBUG(printf("buf_2: n=%zu start=%p content=%p cursor=%p end=%p limit=%p\n",
        n, bstr.start, bstr.content, bstr.cursor, bstr.end, bstr.limit)); 
    assert(n > 0);
    assert(json.type == JSON_Object);
    assert(json.count == 2);
}

int
main()
{
    test_json();
    return 0;  // success!
}
#endif /* TEST_MAIN */
