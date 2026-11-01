#include "world.h"

#include <limits.h> /* INT_MAX */
#include <stdlib.h>
#include <string.h> /* memset */

void world_free(World *w)
{
    /* free(NULL) does nothing, so a half-built world frees cleanly. */
    free(w->terrain);
    free(w->fertility);
    free(w->veg);
    free(w->veg_next);
    free(w->veg_rule);
    free(w->carrion);
    free(w->carrion_level);
    free(w->occupant);
    /* Clear every field, pointers included, so a second world_free (or a
     * use after free) meets NULL instead of a dangling address. A compound
     * literal, (World){0}, is a temporary struct with every field zero. */
    *w = (World){0};
}

bool world_init(World *w, int width, int height)
{
    /* Start with every pointer NULL, so world_free is safe whatever
     * happens below. */
    *w = (World){0};

    /* Indexes are ints, so the cell count must fit in one. Multiplying in
     * long long first: two large ints multiplied as int could overflow,
     * which is undefined behaviour. */
    if (width < 1 || height < 1 || (long long)width * height > INT_MAX) {
        return false;
    }
    w->w = width;
    w->h = height;
    size_t n = (size_t)width * (size_t)height;

    /* calloc(count, size) multiplies for us and zeroes the memory. Zero
     * bits are 0 for integers and 0.0 for floats on every machine agon
     * targets (IEEE 754 floats). */
    w->terrain = calloc(n, sizeof(uint8_t));
    w->fertility = calloc(n, sizeof(float));
    w->veg = calloc(n, sizeof(uint8_t));
    w->veg_next = calloc(n, sizeof(uint8_t));
    w->veg_rule = calloc(n, sizeof(uint8_t));
    w->carrion = calloc(n, sizeof(float));
    w->carrion_level = calloc(n, sizeof(float));
    w->occupant = malloc(n * sizeof(int32_t));

    if (w->terrain == NULL || w->fertility == NULL || w->veg == NULL ||
        w->veg_next == NULL || w->veg_rule == NULL || w->carrion == NULL ||
        w->carrion_level == NULL || w->occupant == NULL) {
        /* Partial failure: some layers exist and some do not. Free the
         * ones that do, and leave the struct empty. */
        world_free(w);
        return false;
    }

    world_clear(w);
    return true;
}

void world_clear(World *w)
{
    size_t n = (size_t)w->w * (size_t)w->h;
    /* memset fills bytes. Every byte 0 means 0 for these integer and
     * float layers; it could not fill occupant with -1 per element in
     * general, so occupant gets a loop. */
    memset(w->terrain, 0, n * sizeof(uint8_t));
    memset(w->fertility, 0, n * sizeof(float));
    memset(w->veg, 0, n * sizeof(uint8_t));
    memset(w->veg_next, 0, n * sizeof(uint8_t));
    memset(w->veg_rule, 0, n * sizeof(uint8_t));
    memset(w->carrion, 0, n * sizeof(float));
    memset(w->carrion_level, 0, n * sizeof(float));
    for (size_t i = 0; i < n; i++) {
        w->occupant[i] = -1;
    }
}

int world_idx(const World *w, int x, int y)
{
    /* In C, % keeps the sign of the left operand: -1 % 8 is -1. Adding
     * the width and taking % again maps -1 to w - 1. Correct for any
     * x >= -w, enough for neighbours one or a few steps away. */
    int wrapped_x = ((x % w->w) + w->w) % w->w;
    int wrapped_y = ((y % w->h) + w->h) % w->h;
    return wrapped_y * w->w + wrapped_x;
}
