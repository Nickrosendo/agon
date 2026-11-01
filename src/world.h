/*
 * world: the grid the simulation lives on. Each layer is one flat array
 * with one value per cell, row after row; cell (x, y) is at y * w + x.
 * The edges wrap around on both axes.
 */
#ifndef AGON_WORLD_H
#define AGON_WORLD_H

#include <stdbool.h>
#include <stdint.h>

enum { TERRAIN_WATER, TERRAIN_SOIL, TERRAIN_ROCK };
#define VEG_MAX 4

typedef struct {
    int w, h;
    uint8_t *terrain;
    float *fertility;        /* 0..1 */
    uint8_t *veg, *veg_next; /* 0..VEG_MAX */
    uint16_t *veg_age;       /* ticks since the plant on this cell sprouted */
    uint8_t *veg_wither;     /* 0 healthy, else ticks spent withering */
    uint8_t *veg_rule;       /* lesson 9: packed thresholds per cell */
    float *carrion;          /* energy lying on the cell */
    float *carrion_level;    /* food-chain level of that carrion */
    float *scent, *scent_next; /* smell of carrion, spreading from it */
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

#endif
