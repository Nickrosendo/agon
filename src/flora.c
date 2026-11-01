#include "flora.h"

/* Wraps a coordinate that is at most one world width (or height) outside
 * 0..size-1. A compare and an add are much cheaper than the two %
 * operations world_idx needs for any distance, and plant_mass runs this
 * dozens of times per cell. v must lie in -size .. 2*size-1, which holds
 * while the reach is at most the world's width and height (flora_step
 * makes sure of that). */
static int wrap_near(int v, int size)
{
    if (v < 0) {
        return v + size;
    }
    if (v >= size) {
        return v - size;
    }
    return v;
}

/* How many of the eight neighbours of (x, y) carry any vegetation. */
static int planted_neighbours(const World *w, int x, int y)
{
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        int row_start = wrap_near(y + dy, w->h) * w->w;
        for (int dx = -1; dx <= 1; dx++) {
            if (w->veg[row_start + wrap_near(x + dx, w->w)]) {
                count++;
            }
        }
    }
    /* The loop counted the cell itself too; take it back off. */
    if (w->veg[y * w->w + x]) {
        count--;
    }
    return count;
}

/* The total density of every cell within reach steps of (x, y), the cell
 * itself left out: the plant mass competing for the same ground. */
static int plant_mass(const World *w, int x, int y, int reach)
{
    int mass = 0;
    for (int dy = -reach; dy <= reach; dy++) {
        int row_start = wrap_near(y + dy, w->h) * w->w;
        for (int dx = -reach; dx <= reach; dx++) {
            mass += w->veg[row_start + wrap_near(x + dx, w->w)];
        }
    }
    /* Adding the centre inside the loop and taking it off once here
     * keeps the loop free of a test on every cell. */
    return mass - w->veg[y * w->w + x];
}

static uint8_t next_density(uint8_t density, int n, int mass,
                            float fertility, float draw, const Settings *s)
{
    /* The int mass is converted to float for the comparison; it is at most
     * a few hundred, so the conversion is exact. */
    float capacity = fertility * s->veg_capacity;
    int room = (float)mass < capacity;
    if (density == 0) {
        if (!room) {
            return 0;
        }
        float chance = (n >= s->veg_birth_lo && n <= s->veg_birth_hi)
                           ? fertility
                           : s->veg_seed_chance * fertility;
        /* draw is in 0..1 (never 1), so a chance of 1 always sprouts and
         * a chance of 0 never does. */
        return draw < chance ? 1 : 0;
    }
    if ((float)mass > capacity) {
        return (uint8_t)(density - 1); /* over capacity: thin out */
    }
    if (!room) {
        return density; /* exactly at capacity: hold */
    }
    if (n <= s->veg_crowd_max) {
        /* density is promoted to int for the + 1; the cast back to
         * uint8_t is safe because the result is at most VEG_MAX. */
        return density < VEG_MAX ? (uint8_t)(density + 1) : VEG_MAX;
    }
    return (uint8_t)(density - 1); /* density >= 1 here, so no wrap */
}

void flora_step(World *w, Rng *r, const Settings *s)
{
    /* wrap_near needs the reach to be no larger than the world; a tiny
     * world (allowed by world_init) would otherwise be read out of
     * bounds. */
    int reach = s->veg_reach;
    if (reach > w->w) {
        reach = w->w;
    }
    if (reach > w->h) {
        reach = w->h;
    }
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            int i = world_idx(w, x, y);
            if (w->terrain[i] != TERRAIN_SOIL) {
                w->veg_next[i] = 0;
                continue;
            }
            /* Exactly one draw for every empty soil cell, whichever
             * branch uses it, and none for planted cells. The draws then
             * happen in the same order on every run, which is what keeps
             * two runs with the same seed identical. It is a separate
             * statement on purpose: inside a larger expression, C does
             * not say in which order the parts are evaluated. */
            float draw = w->veg[i] == 0 ? rng_unit(r) : 0.0f;
            int n = planted_neighbours(w, x, y);
            int mass = plant_mass(w, x, y, reach);
            w->veg_next[i] =
                next_density(w->veg[i], n, mass, w->fertility[i], draw, s);
        }
    }
    uint8_t *old = w->veg;
    w->veg = w->veg_next;
    w->veg_next = old;
}
