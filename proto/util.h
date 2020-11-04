/*
 * util.h -- utility macros and routines
 */
#ifndef _UTIL_H_
#define _UTIL_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

//#define __inline    /* attribute not supported */
#ifndef __inline
#define __inline    inline __attribute__((always_inline))
#endif

typedef int8_t      __s8;
typedef int16_t     __s16;
typedef int32_t     __s32;
typedef int64_t     __s64;

typedef uint8_t     __u8;
typedef uint16_t    __u16;
typedef uint32_t    __u32;
typedef uint64_t    __u64;

void hexdump(FILE *f, void *buffer, size_t size);

#endif /* _UTIL_H_ */
