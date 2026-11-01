/*
 * flora: vegetation spreading by a neighbour rule (Life with a fertility
 * term). Lesson 7 adds carrion here too.
 */
#ifndef AGON_FLORA_H
#define AGON_FLORA_H

#include "rng.h"
#include "settings.h"
#include "world.h"

/* One step: reads w->veg, writes w->veg_next, then swaps the two. Draws
 * from r once per empty soil cell, in row order, so the same world and
 * generator state always give the same result. */
void flora_step(World *w, Rng *r, const Settings *s);

#endif
