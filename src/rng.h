/*
 * rng: seeded random numbers. The same seed gives the same sequence on
 * every machine and every run, which is what makes worlds repeatable.
 * Never use the C library's rand(): its algorithm differs between systems.
 *
 * The generator is xoshiro256** (Blackman and Vigna), with its state
 * filled from the seed by splitmix64.
 */
#ifndef AGON_RNG_H
#define AGON_RNG_H

#include <stdint.h>

typedef struct {
    uint64_t s[4];
} Rng;

/* One step of splitmix64: advances *state and returns a well-mixed
 * value. Used to fill the generator's state, and as a hash. */
uint64_t rng_splitmix64(uint64_t *state);

void rng_seed(Rng *r, uint64_t seed);
uint64_t rng_next(Rng *r);
uint32_t rng_below(Rng *r, uint32_t n); /* 0 <= result < n, n > 0 */
float rng_unit(Rng *r);                 /* 0 <= result < 1 */
float rng_normal(Rng *r);               /* mean 0, spread 1 */

#endif
