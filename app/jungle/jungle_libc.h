/* SPDX-License-Identifier: GPL-3.0-only */
/* The few C library calls the engine makes: on the FM-1 felucca-bsp's chip/libc.c has them (no <string.h>
 * there); the host tests take the system's. */
#ifndef JUNGLE_LIBC_H
#define JUNGLE_LIBC_H
#if defined(_MSC_VER) || defined(PLAT_HOST) || defined(__EMSCRIPTEN__)
#include <string.h>
#else
void *memcpy(void *d, const void *s, unsigned n);
void *memset(void *d, int c, unsigned n);
#endif
#endif
