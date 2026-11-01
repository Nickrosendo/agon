#include "stats.h"

#include <stdlib.h> /* qsort */

void stats_clear(Stats *st)
{
    *st = (Stats){0};
}

int stats_band(float level)
{
    if (level < STATS_MEAT_LEVEL) {
        return BAND_PLANT;
    }
    return level < STATS_TOP_LEVEL ? BAND_MEAT : BAND_TOP;
}

void history_add(History *h, int value)
{
    h->values[h->pos] = value;
    h->pos = (h->pos + 1) % HISTORY;
    if (h->len < HISTORY) {
        h->len++;
    }
}

int history_at(const History *h, int k)
{
    /* The oldest value sits len slots behind pos. Adding HISTORY before
     * taking % keeps the left side from going negative: in C, -3 % 64 is
     * -3, not 61. */
    int oldest = (h->pos - h->len + HISTORY) % HISTORY;
    return h->values[(oldest + k) % HISTORY];
}

/*
 * qsort's comparison function. qsort works on any type, so it hands over
 * two "const void *" pointers to elements; this function knows they are
 * uint32_t and converts them back. It returns a negative number, 0 or a
 * positive number for less, equal or greater. "return x - y" would be
 * wrong here: for unsigned numbers the difference wraps instead of going
 * negative, and converted to int it can have either sign.
 */
static int compare_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y); /* each comparison is 0 or 1 */
}

/* How many different values the first n entries of v hold. Sorts v. */
static int count_distinct(uint32_t *v, int n)
{
    if (n == 0) {
        return 0;
    }
    qsort(v, (size_t)n, sizeof(v[0]), compare_u32);
    int distinct = 1;
    for (int i = 1; i < n; i++) {
        if (v[i] != v[i - 1]) {
            distinct++; /* sorted, so equal values sit side by side */
        }
    }
    return distinct;
}

static void sample_creatures(Stats *st, const Pool *p, uint32_t *scratch)
{
    int n = 0;
    for (int i = 0; i < p->capacity; i++) {
        const Creature *c = &p->items[i];
        if (!c->alive) {
            continue;
        }
        st->by_level[stats_band(c->level)]++;
        if (c->age > st->oldest_age) {
            st->oldest_age = c->age;
        }
        scratch[n] = c->lineage;
        n++;
    }
    st->creatures = n;
    st->lineages = count_distinct(scratch, n);
}

static void sample_world(Stats *st, const World *w)
{
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->veg[i] > 0) {
            st->planted++;
            st->veg_total += w->veg[i];
            if (w->veg_wither[i] > 0) {
                st->withering++;
            }
        }
        if (w->carrion[i] > 0.0f) {
            st->carrion_cells++;
            st->carrion_total += w->carrion[i];
        }
    }
}

void stats_sample(Stats *st, const World *w, const Pool *p, uint64_t tick,
                  uint32_t *scratch)
{
    /* Zero the counts but keep the histories: copy them out, clear the
     * whole struct, copy them back. Each History is a struct holding an
     * array, so each assignment copies all HISTORY values. */
    History plant = st->hist_plant_eaters;
    History meat = st->hist_meat_eaters;
    History planted = st->hist_planted;
    stats_clear(st);
    st->hist_plant_eaters = plant;
    st->hist_meat_eaters = meat;
    st->hist_planted = planted;

    st->tick = tick;
    sample_creatures(st, p, scratch);
    sample_world(st, w);
    history_add(&st->hist_plant_eaters, st->by_level[BAND_PLANT]);
    history_add(&st->hist_meat_eaters,
                st->by_level[BAND_MEAT] + st->by_level[BAND_TOP]);
    history_add(&st->hist_planted, st->planted);
}
