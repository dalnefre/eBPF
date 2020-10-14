/*
 * util.h -- utility routines
 */
#ifndef _UTIL_H_
#define _UTIL_H_

#include <stddef.h>
#include <stdio.h>

void hexdump(FILE *f, void *buffer, size_t size);

#endif /* _UTIL_H_ */
