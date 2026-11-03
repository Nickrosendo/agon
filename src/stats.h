/*
 * stats: numbers about the whole world, sampled every few ticks, plus a
 * short history of a few of them for the panel's graphs. Reads the world
 * and the pool; never changes them, and never draws a random number, so
 * sampling cannot change what happens in the simulation.
 */
#ifndef AGON_STATS_H
#define AGON_STATS_H

#include <stdint.h>

#include "creature.h"
#include "world.h"

/* How many samples each history keeps. */
#define HISTORY 64

/* Food-chain levels split the creatures into three bands: plant-eaters
 * (below STATS_MEAT_LEVEL: more than half their food was plants),
 * meat-eaters (from STATS_MEAT_LEVEL to below STATS_TOP_LEVEL), and top
 * hunters (STATS_TOP_LEVEL and up: they mostly eat meat-eaters). */
#define STATS_MEAT_LEVEL LEVEL_MEAT
#define STATS_TOP_LEVEL 3.5f
enum { BAND_PLANT, BAND_MEAT, BAND_TOP, BAND_COUNT };

/*
 * A ring buffer: HISTORY slots written in turn, the oldest overwritten
 * once all are full. pos is the slot the next sample goes into; len is
 * how many slots hold a sample (it stops growing at HISTORY). Nothing
 * moves when a sample is added: only pos advances, wrapping to 0.
 */
typedef struct {
    int values[HISTORY];
    int pos, len;
} History;

typedef struct {
    uint64_t tick;      /* when this sample was taken */
    int creatures;
    int lineages;       /* distinct founding ancestors among the living */
    int by_level[BAND_COUNT];
    uint32_t oldest_age;
    int planted;        /* cells with any vegetation */
    int64_t veg_total;  /* the sum of every cell's density */
    int withering;      /* planted cells that are withering */
    int carrion_cells;
    float carrion_total;
    /* One history per graph on the panel. */
    History hist_plant_eaters, hist_meat_eaters, hist_planted;
} Stats;

/* Empties *st, histories included. */
void stats_clear(Stats *st);

/* Takes a sample of the world and the pool at the given tick and adds it
 * to the histories. scratch must have room for p->capacity values; it is
 * used to count lineages and holds nothing afterwards. */
void stats_sample(Stats *st, const World *w, const Pool *p, uint64_t tick,
                  uint32_t *scratch);

/* The same numbers without adding to the histories: for a world just
 * loaded, whose histories came from the file. */
void stats_measure(Stats *st, const World *w, const Pool *p, uint64_t tick,
                   uint32_t *scratch);

/* The band a food-chain level falls in: a BAND_ value. */
int stats_band(float level);

/* Adds a value to the ring. */
void history_add(History *h, int value);
/* The k-th value held, oldest first: k = 0 is the oldest, k = len - 1
 * the newest. k must be in 0 .. len - 1. */
int history_at(const History *h, int k);

#endif
