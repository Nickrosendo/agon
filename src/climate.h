/*
 * climate: rain from the world's seed, the water soil holds, a temperature
 * field, and the region each cell belongs to because of them. Nothing here
 * draws from the simulation's random numbers once the world is built: rain is
 * a pure function of the seed, the place and the tick.
 */
#ifndef AGON_CLIMATE_H
#define AGON_CLIMATE_H

#include <stdint.h>

#include "rng.h"
#include "settings.h"
#include "world.h"

/*
 * What a cell counts as for the rules that reward a body. REGION_NONE is for
 * deep water and rock, where nobody lives. It is -1 so that it can never be
 * mistaken for a real region, and that means it is NOT safe as an array index:
 * check for it before writing counts[region].
 */
enum {
    REGION_NONE = -1,
    REGION_COLD,
    REGION_HOT_DRY,
    REGION_TEMPERATE,
    REGION_WET,
    REGION_SHALLOW,
    REGION_COUNT
};

/* Fills w->temperature, w->rain_seed and each layer's first values. Draws from
 * r, so the same generator state always gives the same climate. */
void climate_generate(World *w, Rng *r, const Settings *s);

/* One tick. Every climate_every ticks it refreshes w->rain and updates the
 * water in the soil by that many ticks' worth; other ticks do nothing. */
void climate_step(World *w, const Settings *s, uint64_t tick);

/* How hard it rains on a cell at a tick: 0 (dry) to 1. Depends only on the
 * seed, the place and the tick, so asking changes nothing. */
float climate_rain_at(const World *w, const Settings *s, int x, int y,
                      uint64_t tick);

/* The cell's region, or REGION_NONE. Worked out each time, never stored,
 * because water keeps changing. */
int climate_region(const World *w, int cell, const Settings *s);

#endif
