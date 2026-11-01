#include "flora.h"

/* How many of the eight neighbours of (x, y) carry any vegetation. */
static int planted_neighbours(const World *w, int x, int y)
{
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if ((dx != 0 || dy != 0) && w->veg[world_idx(w, x + dx, y + dy)]) {
                count++;
            }
        }
    }
    return count;
}

static uint8_t next_density(uint8_t density, int n, float fertility,
                            float draw, const Settings *s)
{
    if (density == 0) {
        float chance = (n >= s->veg_birth_lo && n <= s->veg_birth_hi)
                           ? fertility
                           : s->veg_seed_chance * fertility;
        /* draw is in 0..1 (never 1), so a chance of 1 always sprouts and
         * a chance of 0 never does. */
        return draw < chance ? 1 : 0;
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
            w->veg_next[i] =
                next_density(w->veg[i], n, w->fertility[i], draw, s);
        }
    }
    uint8_t *old = w->veg;
    w->veg = w->veg_next;
    w->veg_next = old;
}
