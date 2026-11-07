/*
 * world: the grid the simulation lives on. Each layer is one flat array
 * with one value per cell, row after row; cell (x, y) is at y * w + x.
 * The edges wrap around on both axes.
 */
#ifndef AGON_WORLD_H
#define AGON_WORLD_H

#include <stdbool.h>
#include <stdint.h>

/* TERRAIN_WATER is deep. TERRAIN_SHALLOW is the band beside the shore: anyone
 * can cross it, at a cost, and it is a region of its own. Appended, so the
 * numbers of the others do not change. */
enum { TERRAIN_WATER, TERRAIN_SOIL, TERRAIN_ROCK, TERRAIN_SHALLOW };
#define VEG_MAX 4

/*
 * A plant on one cell: what it has stored and what it inherited. Only
 * meaningful while the cell's veg is above 0; on bare soil it is whatever the
 * last plant left there, and the next sprout overwrites all of it.
 *
 * Seven floats and nothing else, so no padding: sizeof(Plant) is 28 on every
 * system this builds for, and hashing its raw bytes is safe. A cell full of
 * zeroes is a plant offering only leaves, which is what a bare test needs.
 */
typedef struct {
    float energy;     /* stored, 0..plant_energy_max; at 0 the plant withers */
    float fruit;      /* ripe fruit hanging on it; a bite needs a whole one */
    /* The genes, each 0..1. */
    float fruit_share; /* above 0.5 it offers fruit, otherwise leaves */
    float water_need;  /* more grows faster, but needs wetter soil */
    float temp_pref;   /* the temperature it grows best in */
    float gut_time;    /* how long a seed stays inside the animal that ate it */
    /* How far a leaf tree has grown, 0..tree_size_max (a fruit tree stays 0).
     * It grows a little each spring and summer, and from tree_shade_size on it
     * shades the cells around it. A seed starts at 0. */
    float size;
} Plant;

/* What a plant offers is one thing or the other, as in the design: leaves that
 * can be eaten, or fruit. The gene is a float so that a seed can drift towards
 * the line one small step at a time before it crosses. static inline: see
 * "static inline in a header" in the study docs; every file gets its own copy. */
static inline bool plant_offers_fruit(const Plant *p)
{
    return p->fruit_share > 0.5f;
}

typedef struct {
    int w, h;
    uint8_t *terrain;
    float *fertility;        /* 0..1 */
    uint8_t *veg, *veg_next; /* 0..VEG_MAX */
    uint16_t *veg_age;       /* ticks since the plant on this cell sprouted */
    uint8_t *veg_wither;     /* 0 healthy, else ticks spent withering */
    Plant *plant;            /* the living plant on each cell */
    float *carrion;          /* energy lying on the cell */
    float *carrion_level;    /* food-chain level of that carrion */
    float *scent, *scent_next; /* smell of carrion, spreading from it */
    float *water;            /* in the soil, 0..1; 0 on anything else */
    float *temperature;      /* 0 coldest .. 1 hottest, fixed from the start */
    float *rain;             /* how hard it rains, 0..1, as of the last update */
    float *snow;             /* winter's rain kept on the ground, 0..1; 0 on
                                anything but soil; melts into water in spring */
    uint64_t rain_seed;      /* the rain's whole future comes from this */
    int32_t *occupant;       /* creature index, or -1 */
} World;

/* Allocates every layer: occupant set to -1, everything else to 0.
 * Returns false for a width or height below 1, a world too large to
 * index with an int, or when memory runs out; nothing is left allocated
 * then. On success the world must later be given to world_free. */
bool world_init(World *w, int width, int height);
/* Puts every layer back to its starting value (occupant -1, the rest 0)
 * without reallocating, so a new world can reuse the memory. */
void world_clear(World *w);
/* Frees every layer and sets the pointers to NULL; safe to call twice. */
void world_free(World *w);
int world_idx(const World *w, int x, int y); /* wraps both axes */
/* Whether a body can stand on this ground at all: soil or shallow water. */
bool world_walkable(const World *w, int cell);

#endif
