#include "rng.h"

#include <math.h> /* sqrtf, logf, cosf; needs -lm when linking */

/* Unsigned arithmetic in C wraps around modulo 2^64 by definition, and
 * these algorithms depend on it: the multiplications below overflow on
 * purpose. With signed integers the same overflow would be undefined
 * behaviour. */

uint64_t rng_splitmix64(uint64_t *state)
{
    *state += 0x9E3779B97F4A7C15ULL; /* ULL: an unsigned long long literal */
    uint64_t z = *state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/* Rotate left: the bits pushed out on the left come back on the right.
 * C has no rotate operator; this pattern compiles to one instruction. k
 * must be 1..63, since shifting a 64-bit value by 64 is undefined. */
static uint64_t rotate_left(uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

void rng_seed(Rng *r, uint64_t seed)
{
    /* xoshiro must not start from an all-zero state; splitmix64 never
     * returns four zeros in a row, so any seed (0 included) is fine. */
    uint64_t state = seed;
    for (int i = 0; i < 4; i++) {
        r->s[i] = rng_splitmix64(&state);
    }
}

uint64_t rng_next(Rng *r)
{
    /* r->s is shorthand for (*r).s: follow the pointer, then take the
     * field. */
    uint64_t *s = r->s;
    uint64_t result = rotate_left(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotate_left(s[3], 45);
    return result;
}

uint32_t rng_below(Rng *r, uint32_t n)
{
    /* rng_next() % n alone would favour small results slightly, because
     * 2^64 is not a multiple of n. Draws at or above the largest multiple
     * of n are thrown away and redrawn; that happens less than once in
     * 2^32 draws. */
    uint64_t limit = UINT64_MAX - UINT64_MAX % n;
    uint64_t x;
    do {
        x = rng_next(r);
    } while (x >= limit);
    return (uint32_t)(x % n);
}

float rng_unit(Rng *r)
{
    /* A float has 24 bits of precision. Keep the top 24 bits of a draw
     * (0 .. 2^24 - 1) and divide by 2^24, giving 0 <= result < 1 with
     * every value equally likely. Dividing all 64 bits instead could round
     * up to exactly 1.0. */
    uint32_t top = (uint32_t)(rng_next(r) >> 40);
    return (float)top / 16777216.0f;
}

float rng_normal(Rng *r)
{
    /* Box-Muller: two uniform numbers become one normally distributed
     * number. u1 must not be 0, because log(0) is minus infinity; 1 - unit
     * gives 0 < u1 <= 1. */
    const float two_pi = 6.28318530718f; /* C has no standard pi constant */
    float u1 = 1.0f - rng_unit(r);
    float u2 = rng_unit(r);
    return sqrtf(-2.0f * logf(u1)) * cosf(two_pi * u2);
}
