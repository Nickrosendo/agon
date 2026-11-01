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

/* draw decides sprouting, grow_draw decides growth; both are in 0..1. */
static uint8_t next_density(uint8_t density, int n, int mass,
                            float fertility, float draw, float grow_draw,
                            const Settings *s)
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
                           ? fertility * s->veg_sprout_rate
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
        if (grow_draw >= s->veg_grow_chance) {
            return density; /* room to grow, but not this tick */
        }
        /* density is promoted to int for the + 1; the cast back to
         * uint8_t is safe because the result is at most VEG_MAX. */
        return density < VEG_MAX ? (uint8_t)(density + 1) : VEG_MAX;
    }
    return (uint8_t)(density - 1); /* density >= 1 here, so no wrap */
}

/* veg_wither is a uint8_t, so withering can last at most 255 ticks; a
 * larger setting would make the counter wrap back to 0 ("healthy"). */
static int wither_limit(const Settings *s)
{
    if (s->veg_wither_ticks < 1) {
        return 1;
    }
    return s->veg_wither_ticks > UINT8_MAX ? UINT8_MAX : s->veg_wither_ticks;
}

/* A withering plant: one more tick of withering, or gone. Age and
 * withering are written in place: only this cell reads them, so unlike
 * veg they need no second buffer. */
static uint8_t wither_on(World *w, int i, int limit)
{
    if (w->veg_wither[i] >= limit) {
        w->veg_age[i] = 0;
        w->veg_wither[i] = 0;
        return 0;
    }
    w->veg_wither[i]++;
    return w->veg[i]; /* withering plants neither grow nor shrink */
}

void carrion_add(World *w, int idx, float energy, float level)
{
    if (energy <= 0.0f) {
        return;
    }
    float before = w->carrion[idx];
    float total = before + energy;
    /* A running weighted average: each part counts in proportion to its
     * energy. total > 0 here, since energy > 0 and carrion is never
     * negative, so the division is safe. */
    w->carrion_level[idx] =
        (before * w->carrion_level[idx] + energy * level) / total;
    w->carrion[idx] = total;
}

void carrion_step(World *w, const Settings *s)
{
    int n = w->w * w->h;
    for (int i = 0; i < n; i++) {
        if (w->carrion[i] <= 0.0f) {
            continue;
        }
        float lost = w->carrion[i] * s->carrion_decay;
        w->carrion[i] -= lost;
        float fertility = w->fertility[i] + lost * s->carrion_to_fertility;
        w->fertility[i] =
            fertility > s->fertility_max ? s->fertility_max : fertility;
        if (w->carrion[i] < CARRION_MIN) {
            w->carrion[i] = 0.0f;
            w->carrion_level[i] = 0.0f;
        }
    }
}

void scent_step(World *w, const Settings *s)
{
    for (int y = 0; y < w->h; y++) {
        int up = wrap_near(y - 1, w->h) * w->w;
        int down = wrap_near(y + 1, w->h) * w->w;
        int row = y * w->w;
        for (int x = 0; x < w->w; x++) {
            int left = wrap_near(x - 1, w->w);
            int right = wrap_near(x + 1, w->w);
            float around = (w->scent[up + x] + w->scent[down + x] +
                            w->scent[row + left] + w->scent[row + right]) /
                           4.0f;
            int i = row + x;
            float mixed = (1.0f - s->scent_spread) * w->scent[i] +
                          s->scent_spread * around;
            float next = (1.0f - s->scent_decay) * mixed +
                         s->scent_emit * w->carrion[i];
            /* The same reason as CARRION_MIN: fading by a fraction never
             * reaches 0 on its own, and tiny values are slow to compute
             * with. */
            w->scent_next[i] = next < CARRION_MIN * 0.01f ? 0.0f : next;
        }
    }
    float *old = w->scent;
    w->scent = w->scent_next;
    w->scent_next = old;
}

/* How many cells carry any vegetation. */
static int count_planted(const World *w)
{
    int count = 0;
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->veg[i] > 0) {
            count++;
        }
    }
    return count;
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
    int limit = wither_limit(s);
    /* Counted once, before any cell changes: the cap then treats every
     * cell alike, wherever it sits in the row order. */
    bool full = s->veg_max_cells > 0 && count_planted(w) >= s->veg_max_cells;
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            int i = world_idx(w, x, y);
            if (w->terrain[i] != TERRAIN_SOIL) {
                w->veg_next[i] = 0;
                continue;
            }
            /* Exactly two draws for every soil cell, whichever branch
             * uses them: the first to sprout (empty cells) or to start
             * withering (healthy mature plants), the second to grow. The
             * draws then happen in the same order on every run, which is
             * what keeps two runs with the same seed identical. They are
             * separate statements on purpose: inside a larger
             * expression, C does not say in which order the parts are
             * evaluated. One draw used for two decisions would tie them
             * together: a plant that just missed withering would also
             * be more likely to grow. */
            float draw = rng_unit(r);
            float grow_draw = rng_unit(r);

            if (w->veg[i] > 0 && w->veg_wither[i] > 0) {
                w->veg_next[i] = wither_on(w, i, limit);
                continue;
            }
            if (w->veg[i] > 0) {
                /* A healthy plant ages; the age stops at the largest
                 * uint16_t instead of wrapping back to 0. */
                if (w->veg_age[i] < UINT16_MAX) {
                    w->veg_age[i]++;
                }
                if (w->veg_age[i] >= s->veg_mature_age &&
                    draw < s->veg_wither_chance) {
                    w->veg_wither[i] = 1;
                    w->veg_next[i] = w->veg[i];
                    continue;
                }
            }

            if (w->veg[i] == 0 && full) {
                w->veg_next[i] = 0; /* the planted area is at its cap */
                continue;
            }
            int n = planted_neighbours(w, x, y);
            int mass = plant_mass(w, x, y, reach);
            uint8_t next = next_density(w->veg[i], n, mass, w->fertility[i],
                                        draw, grow_draw, s);
            if (w->veg[i] == 0 || next == 0) {
                /* Bare now, or just sprouted: a fresh start. */
                w->veg_age[i] = 0;
                w->veg_wither[i] = 0;
            }
            w->veg_next[i] = next;
        }
    }
    uint8_t *old = w->veg;
    w->veg = w->veg_next;
    w->veg_next = old;
}
