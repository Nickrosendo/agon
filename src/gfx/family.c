#include "family.h"

#include <math.h>   /* atan2f, cosf, sinf */
#include <stdlib.h> /* calloc, free, qsort */
#include <string.h> /* memmove */

#include "palette.h" /* render_meat_share */

#define TWO_PI 6.2831855f

/* One family's total, while one pass adds its members up. */
typedef struct {
    float size, speed, sight, plant, meat, meat_share;
    float armour, weapon, legs, swim;
    /* Hue is an angle round a wheel, so it is summed as an arrow: see below. */
    float hue_x, hue_y;
    int members;
} FamilySum;

bool family_init(Families *f, int pool_capacity)
{
    *f = (Families){0};
    f->scratch = calloc((size_t)pool_capacity, sizeof(*f->scratch));
    if (f->scratch == NULL) {
        return false;
    }
    f->scratch_len = pool_capacity;
    return true;
}

void family_free(Families *f)
{
    free(f->scratch); /* free(NULL) does nothing, so this is safe to repeat */
    *f = (Families){0};
}

void family_reset(Families *f)
{
    f->used = 0;
    f->passes = 0;
    f->next_tick = 0;
}

/*
 * qsort takes any array, so it hands this function two void pointers and it has
 * to say what they point at. The answer must be negative, zero or positive.
 * Not "return *a - *b": those are unsigned, and the result would wrap round.
 */
static int compare_lineages(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a;
    uint32_t y = *(const uint32_t *)b;
    return (x > y) - (x < y);
}

/* Every living creature's lineage, sorted, so each family is one run. */
static int sorted_lineages(Families *f, const Sim *s)
{
    int n = 0;
    for (int i = 0; i < s->pool.capacity && n < f->scratch_len; i++) {
        if (s->pool.items[i].alive) {
            f->scratch[n] = s->pool.items[i].lineage;
            n++;
        }
    }
    qsort(f->scratch, (size_t)n, sizeof(f->scratch[0]), compare_lineages);
    return n;
}

typedef struct {
    uint32_t lineage;
    int count;
} TopFamily;

/*
 * Adds a family to a list kept largest first, of at most FAMILY_MAX, and returns
 * the new length. Lineages arrive in rising order, so on a tie the older family,
 * already there, stays ahead.
 */
static int top_insert(TopFamily *top, int used, uint32_t lineage, int count)
{
    int at = used;
    while (at > 0 && top[at - 1].count < count) {
        at--;
    }
    if (at == FAMILY_MAX) {
        return used; /* smaller than all of a full list */
    }
    int kept = used < FAMILY_MAX ? used : FAMILY_MAX - 1;
    /* memmove, not memcpy: the two ranges overlap, which memcpy does not allow. */
    memmove(&top[at + 1], &top[at], (size_t)(kept - at) * sizeof(top[0]));
    top[at] = (TopFamily){lineage, count};
    return kept + 1;
}

/* Counts runs in the sorted lineages and keeps the largest. Returns how many. */
static int find_top(const uint32_t *sorted, int n, TopFamily *top)
{
    int used = 0;
    int i = 0;
    while (i < n) {
        int end = i;
        while (end < n && sorted[end] == sorted[i]) {
            end++;
        }
        used = top_insert(top, used, sorted[i], end - i);
        i = end;
    }
    return used;
}

/* The record for this lineage, or NULL. */
static FamilyRecord *record_of(Families *f, uint32_t lineage)
{
    for (int i = 0; i < f->used; i++) {
        if (f->records[i].lineage == lineage) {
            return &f->records[i];
        }
    }
    return NULL;
}

static bool in_top(const TopFamily *top, int n, uint32_t lineage)
{
    for (int i = 0; i < n; i++) {
        if (top[i].lineage == lineage) {
            return true;
        }
    }
    return false;
}

/* Makes the records exactly the top families: old ones out, new ones in. */
static void match_records(Families *f, const TopFamily *top, int n)
{
    /* Removing by moving the last record into the hole: order does not matter,
     * so nothing else has to shift. i stays put to look at the one moved in. */
    int i = 0;
    while (i < f->used) {
        if (in_top(top, n, f->records[i].lineage)) {
            i++;
        } else {
            f->records[i] = f->records[f->used - 1]; /* a struct copy, ~900 bytes */
            f->used--;
        }
    }
    for (int k = 0; k < n; k++) {
        FamilyRecord *r = record_of(f, top[k].lineage);
        if (r == NULL) {
            r = &f->records[f->used];
            f->used++;
            r->lineage = top[k].lineage;
            r->n = 0;
            r->every = 1;
        }
        r->count = top[k].count;
    }
}

/* Keeps samples 0, 2, 4, ... and takes samples half as often from now on. */
static void halve(FamilyRecord *r)
{
    int kept = 0;
    for (int i = 0; i < r->n; i += 2) {
        r->samples[kept] = r->samples[i];
        kept++;
    }
    r->n = kept;
    r->every *= 2;
}

static void add_to_sum(FamilySum *sum, const Creature *c)
{
    BodyTraits t = species_traits(&c->genome);
    sum->size += t.size;
    sum->speed += t.speed;
    sum->sight += t.sight;
    sum->plant += t.plant;
    sum->meat += t.meat;
    sum->armour += t.armour;
    sum->weapon += t.weapon;
    sum->legs += t.legs;
    sum->swim += t.swim;
    sum->meat_share += render_meat_share(c->level);
    float angle = c->genome.g[GENE_HUE] * TWO_PI;
    sum->hue_x += cosf(angle);
    sum->hue_y += sinf(angle);
    sum->members++;
}

/*
 * The average of a hue. Hues 0.99 and 0.01 are neighbours on the wheel, and their
 * plain average, 0.5, is the opposite colour. So each hue becomes an arrow
 * pointing at its place on the wheel, the arrows are added, and the direction of
 * the total is the average.
 */
static float average_hue(const FamilySum *sum)
{
    float hue = atan2f(sum->hue_y, sum->hue_x) / TWO_PI; /* -0.5 .. 0.5 */
    if (hue < 0.0f) {
        hue += 1.0f;
    }
    /* A tiny negative plus 1 can round to exactly 1, which is not a hue. */
    return hue >= 1.0f ? 0.0f : hue;
}

static FamilySample sample_from(const FamilySum *sum)
{
    float n = (float)sum->members;
    FamilySample out;
    out.traits = (BodyTraits){sum->size / n,   sum->speed / n,  sum->sight / n,
                              sum->plant / n,  sum->meat / n,   sum->armour / n,
                              sum->weapon / n, sum->legs / n,   sum->swim / n};
    out.hue = average_hue(sum);
    out.meat_share = sum->meat_share / n;
    return out;
}

/* Adds up every recorded family's members and gives a sample to those due. */
static void take_samples(Families *f, const Sim *s)
{
    FamilySum sums[FAMILY_MAX] = {0};
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (!c->alive) {
            continue;
        }
        FamilyRecord *r = record_of(f, c->lineage);
        if (r != NULL) {
            /* The record's distance from the start of the array is its index. */
            add_to_sum(&sums[r - f->records], c);
        }
    }
    for (int i = 0; i < f->used; i++) {
        FamilyRecord *r = &f->records[i];
        if (r->n == FAMILY_SAMPLES) {
            halve(r);
        }
        if (f->passes % (uint64_t)r->every == 0) {
            r->samples[r->n] = sample_from(&sums[i]);
            r->n++;
        }
    }
}

void family_update(Families *f, const Sim *s)
{
    if (s->tick < f->next_tick) {
        return;
    }
    f->next_tick = s->tick + FAMILY_PASS_TICKS;
    int n = sorted_lineages(f, s);
    TopFamily top[FAMILY_MAX];
    int top_n = find_top(f->scratch, n, top);
    match_records(f, top, top_n);
    take_samples(f, s);
    f->passes++;
}

uint32_t family_largest(const Families *f, const Sim *s)
{
    if (s->pool.count == 0) {
        return FAMILY_NONE;
    }
    const FamilyRecord *best = NULL;
    for (int i = 0; i < f->used; i++) {
        const FamilyRecord *r = &f->records[i];
        if (r->n > 0 && (best == NULL || r->count > best->count ||
                         (r->count == best->count && r->lineage < best->lineage))) {
            best = r;
        }
    }
    return best != NULL ? best->lineage : FAMILY_NONE;
}

int family_strip(const Families *f, uint32_t lineage,
                 FamilySample out[FAMILY_STRIP])
{
    const FamilyRecord *r = NULL;
    for (int i = 0; i < f->used; i++) {
        if (f->records[i].lineage == lineage) {
            r = &f->records[i];
        }
    }
    if (r == NULL) {
        return 0;
    }
    if (r->n <= FAMILY_STRIP) {
        for (int i = 0; i < r->n; i++) {
            out[i] = r->samples[i];
        }
        return r->n;
    }
    for (int i = 0; i < FAMILY_STRIP; i++) {
        /* i = 0 gives the first and i = 7 the last. Multiplying before dividing:
         * integer division throws the remainder away. */
        out[i] = r->samples[i * (r->n - 1) / (FAMILY_STRIP - 1)];
    }
    return FAMILY_STRIP;
}
