/*
 * bstr.h -- binary-stream routines
 */
#ifndef _BSTR_H_
#define _BSTR_H_

#include "code.h"

typedef struct bstr {
    octet_t     *head;
    octet_t     *base;
    octet_t     *start;
    octet_t     *content;
    octet_t     *cursor;
    octet_t     *end;
    octet_t     *limit;
    octet_t     *tail;
} bstr_t;

typedef union bpage {
    octet_t     page[4096];
    struct {
        struct {
            uint16_t    memo_ofs[256];
            octet_t     memo_idx;
            octet_t     room[511];
        }           head;
        octet_t     data[2048];
        octet_t     tail[1024 - sizeof(bstr_t)];
        bstr_t      meta;
    }           pt;
} bpage_t;

/**
       memo .. data .. 06 10 81 04 83 7F 80 81 .. tail .. meta
       ^       ^       ^           ^  ^        ^  ^       ^
head --+       |       |           |  |        |  |       |
base ----------+       |           |  |        |  |       |
start -----------------+           |  |        |  |       |
content ---------------------------+  |        |  |       |
cursor -------------------------------+        |  |       |
end -------------------------------------------+  |       |
limit --------------------------------------------+       |
tail -----------------------------------------------------+

        Example: encoding array_n = [-1, 0, 1]
**/

extern bstr_t *bpage_init(bpage_t *page);
extern bstr_t *bstr_alloc();
extern bstr_t *bstr_free(bstr_t *bstr);

extern int bstr_put_raw(bstr_t *bstr, octet_t b);
extern int bstr_put_int(bstr_t *bstr, int i);
extern int bstr_put_int16(bstr_t *bstr, int16_t i);
extern int bstr_put_int32(bstr_t *bstr, int32_t i);
extern int bstr_put_int64(bstr_t *bstr, int64_t i);
extern int bstr_put_blob(bstr_t *bstr, void *data, size_t size);
extern int bstr_open_array(bstr_t *bstr);
extern int bstr_open_array_n(bstr_t *bstr, size_t n);
extern int bstr_close_array(bstr_t *bstr);

extern int bstr_get_value(bstr_t *bstr);

#endif /* _BSTR_H_ */
