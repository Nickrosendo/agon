/*
 * gfx_hash: the drawing's own source of variety. The same place always gives
 * the same number, so a picture never flickers, and the simulation's random
 * generator is never touched.
 */
#ifndef AGON_GFX_HASH_H
#define AGON_GFX_HASH_H

#include <stdint.h>

/* static inline: the whole function lives in this header, and each .c file
 * including it gets its own copy, so there is no .c file for it to link to. */
static inline uint32_t gfx_mix(uint32_t n)
{
    /* uint32_t products wrap, as JavaScript's Math.imul does. */
    n = (n ^ (n >> 13)) * 1274126177u;
    return n ^ (n >> 16);
}

/* The mockup's h(x, y, s) (v9:31), 0 <= result < 1. */
static inline float gfx_hash(int x, int y, int s)
{
    /* The casts to unsigned make negative numbers wrap (defined) instead of
     * overflowing (undefined, for a signed int). */
    uint32_t n = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u +
                 (uint32_t)s * 1442695041u;
    return (float)(gfx_mix(n) >> 8) / 16777216.0f; /* 24 bits: exact in a float, never 1 */
}

#endif
