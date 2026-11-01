/*
 * terrain: builds a world's terrain and starting fertility from the
 * seeded generator.
 */
#ifndef AGON_TERRAIN_H
#define AGON_TERRAIN_H

#include <stdint.h>

#include "rng.h"
#include "settings.h"
#include "world.h"

/* Fills w->terrain and w->fertility. Draws from r, so the same generator
 * state always gives the same world. */
void terrain_generate(World *w, Rng *r, const Settings *s);

/* Smooth noise in 0..1 at cell (x, y) of a w by h grid, tiling at the
 * edges. scale is the cells between lattice points in the first layer;
 * each of the octaves layers is twice as fine and half as strong. */
float terrain_noise(uint64_t seed, int x, int y, int w, int h, float scale,
                    int octaves);

#endif
