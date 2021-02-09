/*
 * json.h -- JSON data-types
 */
#ifndef _JSON_H_
#define _JSON_H_

#include "code.h"
#include "bstr.h"

typedef enum {
    JSON_Null,
    JSON_Boolean,
    JSON_Number,
    JSON_String,
    JSON_Array,
    JSON_Object,
} JSON_Type;

typedef enum {
    ENC_Raw,
    ENC_UTF_8,
    ENC_UTF_16be,
    ENC_UTF_16le,
} JSON_SEnc;

typedef struct json {
    bstr_t      *bstr;          // byte-stream reference
    JSON_Type   type;           // JSON value type
    int         index;          // sub-element index (default: 0)
    int         count;          // sub-element count (default: -1)
    union {
        octet_t     raw;            // raw encoded octet
        struct {
            int64_t     bits;
            int64_t     base;
            int64_t     exp;
        } num;                      // number value
        struct {
            int64_t     len;
            JSON_SEnc   enc;
        } str;                      // string value
    } val;                      // type-variadic value
} json_t;

/**
               data .. 06 10 81 04 83 7F 80 81 .. oob
               ^       ^           ^  ^        ^  ^
bstr->base ----+       |           |  |        |  |
bstr->start -----------+           |  |        |  |
bstr->content ---------------------+  |        |  |
bstr->cursor -------------------------+        |  |
bstr->end -------------------------------------+  |
bstr->limit --------------------------------------+

        Example: encoding array_n = [-1, 0, 1]
**/

extern int json_get_value(json_t *json);
extern int json_get_int64(json_t *json);

#endif /* _JSON_H_ */
