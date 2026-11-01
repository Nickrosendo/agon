/*
 * flora: vegetation spreading by a neighbour rule (Life with a fertility
 * term), and carrion rotting back into the soil.
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

/* Lays energy worth of carrion at food-chain level on cell idx. Carrion
 * already there merges with it: the amounts add up and the cell's level
 * becomes the energy-weighted average. Nothing happens for energy <= 0. */
void carrion_add(World *w, int idx, float energy, float level);
/* One tick of rotting on every cell: each loses carrion_decay of its
 * carrion, and carrion_to_fertility of that loss raises the cell's
 * fertility, never above fertility_max. Less than CARRION_MIN left is
 * cleared to exactly 0. */
void carrion_step(World *w, const Settings *s);

/* Below this, carrion counts as gone. Without a cut-off, decay by a
 * fraction would never reach 0: x * 0.99 stays above 0 for thousands of
 * ticks, down to the smallest float. */
#define CARRION_MIN 0.01f

#endif
