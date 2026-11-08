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
 * from r three times per soil cell, in row order and in every season, so the
 * same world and generator state always give the same result. season is a
 * SEASON_ value (years.h): the season of the tick being stepped. */
void flora_step(World *w, Rng *r, const Settings *s, int season);
/* How many cells hold a plant, withering ones included. */
int flora_count_planted(const World *w);

/* Whether a plant takes part in this season: gains energy, grows, seeds and
 * makes fruit. Every plant rests in winter, and a fruit tree from autumn on. */
bool plant_is_growing(const Plant *p, int season);
/* Whether a leaf plant stands without leaves: in autumn and winter. A bare
 * plant offers nothing to bite but is still alive. Fruit trees are never bare:
 * their hanging fruit stays and can be eaten. */
bool plant_is_bare(const Plant *p, int season);

/* Whether this plant shades the cells around it: a leaf plant that has grown to
 * tree_shade_size. A fruit tree never does, and size stays 0 on one anyway. */
bool plant_gives_shade(const Plant *p, const Settings *s);

/* How well a cell with this water and temperature suits a plant, 0..1. See
 * Settings for the sum. Too dry or too cold for it reads as a low number, never
 * as a fixed yes or no: only plant_seed_fit makes a line of it. */
float plant_fit(const Plant *p, float water, float temperature,
                const Settings *s);
/* Writes into *seed a copy of the parent's genes, each maybe mutated, with no
 * energy and no fruit. Draws from r. */
void plant_make_seed(Plant *seed, const Plant *parent, Rng *r, const Settings *s);
/* How long this seed stays inside an animal, at least 1. */
uint16_t plant_gut_ticks(const Plant *seed, const Settings *s);
/* Puts a sprout on the cell with this much energy. False, and nothing changes,
 * unless the cell is bare soil. */
bool plant_sow(World *w, int cell, const Plant *seed, float energy);

/* Lays energy worth of carrion at food-chain level on cell idx. Carrion
 * already there merges with it: the amounts add up and the cell's level
 * becomes the energy-weighted average. Nothing happens for energy <= 0. */
void carrion_add(World *w, int idx, float energy, float level);
/* One tick of rotting on every cell: each loses carrion_decay of its
 * carrion, and carrion_to_fertility of that loss raises the cell's
 * fertility, never above fertility_max. Less than CARRION_MIN left is
 * cleared to exactly 0. */
void carrion_step(World *w, const Settings *s);

/* One tick of scent: spreads to the four neighbours, fades, and is fed
 * by the carrion on each cell (see Settings). Reads scent, writes
 * scent_next, then swaps the two, as flora_step does with vegetation:
 * every cell must see its neighbours' scent from before this tick. */
void scent_step(World *w, const Settings *s);

/* Below this, carrion counts as gone. Without a cut-off, decay by a
 * fraction would never reach 0: x * 0.99 stays above 0 for thousands of
 * ticks, down to the smallest float. */
#define CARRION_MIN 0.01f

#endif
