#include "flora.h"

#include <math.h> /* fabsf */

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

/* The index of the k-th planted neighbour of (x, y), counted in the same
 * order as planted_neighbours: row by row, the cell itself skipped. k
 * must be below the count planted_neighbours gives. */
static int planted_neighbour_at(const World *w, int x, int y, int k)
{
    for (int dy = -1; dy <= 1; dy++) {
        int row_start = wrap_near(y + dy, w->h) * w->w;
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            int idx = row_start + wrap_near(x + dx, w->w);
            if (w->veg[idx]) {
                if (k == 0) {
                    return idx;
                }
                k--;
            }
        }
    }
    return -1; /* only reached if k was too large */
}

/* Which planted neighbour is the parent. % on 2^32 values favours the smallest
 * results by at most count in 2^32 (rng_below explains why), far too little to
 * matter here. Only the low 32 bits of the draw are used. */
static int draw_parent(uint64_t draw, int count)
{
    return (int)((uint32_t)draw % (uint32_t)count);
}

static float clamp_unit(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float plant_fit(const Plant *p, float water, float temperature,
                const Settings *s)
{
    /* plant_water_min is at least 0.001 (the settings table), so the division
     * is safe, and water 0 always gives a fit of 0: no plant lives on dust. */
    float need = s->plant_water_min + (1.0f - s->plant_water_min) * p->water_need;
    /* Fruit is mostly water: a tree needs wetter soil than a leaf plant with the
     * same gene, and that is what leaves dry ground to the leaf plants. */
    if (plant_offers_fruit(p)) {
        need += s->plant_fruit_thirst;
    }
    float wet = clamp_unit(water / need);
    float warm =
        clamp_unit(1.0f - fabsf(temperature - p->temp_pref) / s->plant_temp_width);
    return wet * warm;
}

/* A gene's mutation: a normal step, kept inside 0..1. Two draws whether or not
 * it mutates, so the count of draws does not depend on the outcome. */
static float mutated_gene(float gene, Rng *r, const Settings *s)
{
    float chance = rng_unit(r);
    float step = rng_normal(r) * s->plant_mutation_spread;
    return chance < s->plant_mutation ? clamp_unit(gene + step) : gene;
}

void plant_make_seed(Plant *seed, const Plant *parent, Rng *r, const Settings *s)
{
    /* Separate statements, so the draws have a fixed order. seed and parent must
     * be two different plants: seed is written as parent is read. */
    seed->energy = 0.0f;
    seed->fruit = 0.0f;
    seed->fruit_share = mutated_gene(parent->fruit_share, r, s);
    seed->water_need = mutated_gene(parent->water_need, r, s);
    seed->temp_pref = mutated_gene(parent->temp_pref, r, s);
    seed->gut_time = mutated_gene(parent->gut_time, r, s);
}

uint16_t plant_gut_ticks(const Plant *seed, const Settings *s)
{
    int lowest = s->plant_gut_min;
    int highest = s->plant_gut_max > lowest ? s->plant_gut_max : lowest;
    /* The table keeps both within 1..65535, and gut_time within 0..1 keeps the
     * sum between them, so the cast to uint16_t loses nothing but the fraction. */
    float ticks = (float)lowest + seed->gut_time * (float)(highest - lowest);
    return (uint16_t)ticks;
}

bool plant_sow(World *w, int cell, const Plant *seed, float energy)
{
    if (w->terrain[cell] != TERRAIN_SOIL || w->veg[cell] > 0) {
        return false;
    }
    /* Assigning a struct copies all of it: seed's own memory is left alone. */
    w->plant[cell] = *seed;
    w->plant[cell].energy = energy;
    w->plant[cell].fruit = 0.0f;
    w->veg[cell] = 1;
    w->veg_age[cell] = 0;
    w->veg_wither[cell] = 0;
    return true;
}

/* One tick of a living plant: what it gains, then what it pays, then any fruit,
 * paid for out of energy above plant_fruit_energy. */
static void plant_live(Plant *p, float fit, const Settings *s)
{
    float vigour = s->plant_thrift + (1.0f - s->plant_thrift) * p->water_need;
    float energy = p->energy + s->plant_gain * fit * vigour - s->plant_upkeep;
    energy = energy > s->plant_energy_max ? s->plant_energy_max : energy;
    energy = energy < 0.0f ? 0.0f : energy;
    if (fit >= s->plant_seed_fit && energy >= s->plant_fruit_energy &&
        p->fruit < s->plant_fruit_max && plant_offers_fruit(p)) {
        float made = s->plant_fruit_rate;
        p->fruit += made;
        energy -= made * s->plant_fruit_cost;
        energy = energy < 0.0f ? 0.0f : energy;
    }
    p->energy = energy;
}

/* Whether the plant on this cell can give a seed right now. */
static bool can_seed(const World *w, int i, const Settings *s)
{
    return w->plant[i].energy >= s->plant_seed_energy &&
           plant_fit(&w->plant[i], w->water[i], w->temperature[i], s) >=
               s->plant_seed_fit;
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

/* An empty cell's next density. draw is in 0..1 (never 1), so a chance of 1
 * always sprouts and a chance of 0 never does. An empty cell with no planted
 * neighbour never comes here: a plant comes only from a plant. */
static uint8_t sprout_density(int mass, float fertility, float draw,
                              float near, const Settings *s)
{
    /* The int mass is converted to float for the comparison; it is at most
     * a few hundred, so the conversion is exact. */
    if ((float)mass >= fertility * s->veg_capacity) {
        return 0;
    }
    return draw < fertility * s->veg_sprout_rate * near ? 1 : 0;
}

/* A planted cell's next density. can_grow says whether the plant is well enough
 * and can pay for it. */
static uint8_t planted_density(uint8_t density, int mass, float fertility,
                               float grow_draw, bool can_grow, const Settings *s)
{
    float capacity = fertility * s->veg_capacity;
    if ((float)mass > capacity) {
        return (uint8_t)(density - 1); /* over capacity: thin out */
    }
    if ((float)mass == capacity || !can_grow || grow_draw >= s->veg_grow_chance) {
        return density;
    }
    /* density is promoted to int for the + 1; the cast back to uint8_t is safe
     * because the result is at most VEG_MAX. */
    return density < VEG_MAX ? (uint8_t)(density + 1) : VEG_MAX;
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
            /* Exactly three draws for every soil cell, whichever branch
             * uses them: the first to sprout (empty cells) or to start
             * withering (healthy mature plants), the second to grow, the
             * third to pick a sprout's parent. They are separate statements
             * on purpose: inside a larger expression, C does not say in which
             * order the parts are evaluated. One draw used for two decisions
             * would tie them together: a plant that just missed withering
             * would also be more likely to grow. A seed's genes draw more, but
             * only when a sprout really happens. */
            float draw = rng_unit(r);
            float grow_draw = rng_unit(r);
            uint64_t parent_draw = rng_next(r);

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
                Plant *plant = &w->plant[i];
                float fit = plant_fit(plant, w->water[i], w->temperature[i], s);
                plant_live(plant, fit, s);
                bool old_age = w->veg_age[i] >= s->veg_mature_age &&
                               draw < s->veg_wither_chance;
                if (old_age || plant->energy <= 0.0f) {
                    w->veg_wither[i] = 1;
                    w->veg_next[i] = w->veg[i];
                    continue;
                }
                int mass = plant_mass(w, x, y, reach);
                bool can_grow = fit >= s->plant_seed_fit &&
                                plant->energy >= s->plant_grow_cost;
                uint8_t next = planted_density(w->veg[i], mass, w->fertility[i],
                                               grow_draw, can_grow, s);
                if (next > w->veg[i]) {
                    plant->energy -= s->plant_grow_cost;
                }
                if (next == 0) {
                    w->veg_age[i] = 0;
                    w->veg_wither[i] = 0;
                }
                w->veg_next[i] = next;
                continue;
            }

            /* Bare soil. */
            w->veg_age[i] = 0;
            w->veg_wither[i] = 0;
            w->veg_next[i] = 0;
            if (full) {
                continue; /* the planted area is at its cap */
            }
            int n = planted_neighbours(w, x, y);
            if (n == 0) {
                continue;
            }
            int parent =
                planted_neighbour_at(w, x, y, draw_parent(parent_draw, n));
            if (!can_seed(w, parent, s)) {
                continue;
            }
            int mass = plant_mass(w, x, y, reach);
            /* A fruit tree spreads mostly through the animals that eat its
             * fruit: few of its seeds fall right beside it. */
            float near = plant_offers_fruit(&w->plant[parent]) ? s->plant_fruit_near
                                                               : 1.0f;
            uint8_t next = sprout_density(mass, w->fertility[i], draw, near, s);
            if (next > 0) {
                /* Written in place, without a second buffer, and still safe: a
                 * parent is a cell planted before this tick (w->veg is the old
                 * buffer), and this cell was empty, so no later cell can pick
                 * it as a parent this tick. A plant that dies keeps its Plant
                 * for the same reason: its neighbours may still pick it. The
                 * parent's energy does change as it pays, so a plant late in
                 * the row order sees it already paid; the order is fixed, so
                 * two runs still agree. */
                Plant *from = &w->plant[parent];
                plant_make_seed(&w->plant[i], from, r, s);
                w->plant[i].energy = s->plant_seed_start;
                float left = from->energy - s->plant_seed_cost;
                from->energy = left < 0.0f ? 0.0f : left;
            }
            w->veg_next[i] = next;
        }
    }
    uint8_t *old = w->veg;
    w->veg = w->veg_next;
    w->veg_next = old;
}
