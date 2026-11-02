/* SPDX-License-Identifier: GPL-3.0-only */
/* 64-bit multiplies, kept out of line. On the FM-1 an inlined 32x32->64 multiply among 16-bit loads now and then
 * leaves a register one bit off and the firmware crashes; in a function of its own the product is used at once and
 * it does not happen (felucca-bsp docs/CPU.md). Every 64-bit multiply in the engine goes through these. */
#ifndef JUNGLE_MUL_H
#define JUNGLE_MUL_H
#include <stdint.h>

#define JUNGLE_MUL static __attribute__((noinline, unused))
/* (a * b) >> shift */
JUNGLE_MUL int32_t mul_shr(int32_t a, int32_t b, unsigned shift) { return (int32_t)(((int64_t)a * b) >> shift); }
JUNGLE_MUL uint32_t umul_shr(uint32_t a, uint32_t b, unsigned shift) { return (uint32_t)(((uint64_t)a * b) >> shift); }
/* (a * b + add) >> shift */
JUNGLE_MUL int32_t mul_add_shr(int32_t a, int32_t b, int32_t add, unsigned shift)
{
    return (int32_t)(((int64_t)a * b + add) >> shift);
}
/* the whole product, for sums */
JUNGLE_MUL int64_t mul64(int32_t a, int32_t b) { return (int64_t)a * b; }
JUNGLE_MUL uint64_t umul64(uint32_t a, uint32_t b) { return (uint64_t)a * b; }
#endif
