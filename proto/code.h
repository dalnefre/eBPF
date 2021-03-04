/*
 * code.h -- data encoding/decoding
 */
#ifndef _CODE_H_
#include "../include/code.h"  // #define's _CODE_H_

#include <stdint.h>

extern size_t encode_int(octet_t *buffer, size_t limit, int data);
extern size_t encode_int_fixed(octet_t *buffer, size_t width, int data);
extern size_t encode_cstr(octet_t *buffer, size_t limit, char *s);
extern size_t encode_blob(octet_t *buffer, size_t limit, void *data, size_t size);
extern size_t encode_array_of_int(octet_t *buffer, size_t limit, int *data, size_t count);

extern size_t decode_int(octet_t *buffer, size_t limit, int *data);
extern size_t decode_int64(octet_t *buffer, size_t limit, int64_t *data);
extern size_t decode_cstr(octet_t *buffer, size_t limit, char *data, size_t size);

#endif /* _CODE_H_ */
