/* SPDX-License-Identifier: GPL-3.0-only */
/* The few C library calls the engine makes: on the FM-1 felucca-bsp's chip/libc.c has them (no <string.h>
 * there); a host build (the tests, fm1-base's simulator) takes the system's. */
#ifndef JUNGLE_LIBC_H
#define JUNGLE_LIBC_H
#if defined(_MSC_VER) || (defined(__has_include) && __has_include(<string.h>))
#include <string.h>
#else
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
#endif
#endif
