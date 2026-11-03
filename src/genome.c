#include "genome.h"

#include <math.h> /* fabsf, floorf, isfinite */

/* Two values "nearly match" for genome_kinship when they differ by less
 * than this. Random genomes rarely match (about 1 value in 20); a child
 * keeps every value it did not mutate. */
#define KIN_TOLERANCE 0.05f

static float clamp(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

/* Hue is an angle round the colour wheel stored as 0..1, so 1.0 is the
 * same colour as 0.0: a step past either end comes back in at the other. */
static float wrap01(float v)
{
    v -= floorf(v); /* floorf(-0.2f) is -1.0f, so -0.2 becomes 0.8 */
    /* A tiny negative v gives 1 - tiny, which rounds to exactly 1.0f in a
     * float; 1.0 is outside 0..1 (1 excluded) but means 0. */
    return v >= 1.0f ? 0.0f : v;
}

void genome_random(Genome *g, Rng *r, const Settings *s)
{
    for (int i = 0; i < GENE_TRAITS; i++) {
        g->g[i] = rng_unit(r); /* 0 <= x < 1, so hue is below 1 too */
    }
    for (int i = GENE_TRAITS; i < GENOME_LEN; i++) {
        float w = rng_normal(r) * s->weight_start_spread;
        g->g[i] = clamp(w, -s->weight_limit, s->weight_limit);
    }
}

/* The size of one value's whole range: the step a mutation takes is
 * mutation_spread times this, so traits and weights mutate alike. */
static float gene_range(int i, const Settings *s)
{
    return i < GENE_TRAITS ? 1.0f : 2.0f * s->weight_limit;
}

/* Brings a mutated value back into its range. */
static float gene_fix(int i, float v, float before, const Settings *s)
{
    /* NaN (not a number) or infinity would spread to every child and
     * every network output. Every comparison with NaN is false, so the
     * clamp below would let a NaN straight through: it must be caught
     * first. */
    if (!isfinite(v)) {
        return before;
    }
    if (i == GENE_HUE) {
        return wrap01(v);
    }
    if (i < GENE_TRAITS) {
        return clamp(v, 0.0f, 1.0f);
    }
    return clamp(v, -s->weight_limit, s->weight_limit);
}

static void genome_mutate(Genome *g, Rng *r, const Settings *s)
{
    for (int i = 0; i < GENOME_LEN; i++) {
        /* One draw per value whether or not it mutates, so the number of
         * draws, and with it every later random number, does not depend
         * on which values changed. */
        if (rng_unit(r) >= s->mutation_rate) {
            continue;
        }
        float before = g->g[i];
        float step = rng_normal(r) * s->mutation_spread * gene_range(i, s);
        g->g[i] = gene_fix(i, before + step, before, s);
    }
}

/* One draw: which parent this part comes from. */
static const Genome *pick_parent(const Genome *a, const Genome *b, Rng *r)
{
    return rng_below(r, 2) == 0 ? a : b;
}

void genome_cross(Genome *child, const Genome *a, const Genome *b, Rng *r,
                  const Settings *s)
{
    for (int i = 0; i < GENE_TRAITS; i++) {
        child->g[i] = pick_parent(a, b, r)->g[i];
    }
    /* A hidden value's group: its row of weights in, bias included (so <=),
     * and one weight out to each output. */
    for (int h = 0; h < NET_HIDDEN; h++) {
        const Genome *from = pick_parent(a, b, r);
        for (int i = 0; i <= NET_IN; i++) {
            int at = net_hidden_weight(h, i);
            child->g[at] = from->g[at];
        }
        for (int o = 0; o < NET_OUT; o++) {
            int at = net_out_weight(o, h);
            child->g[at] = from->g[at];
        }
    }
    /* The outputs' biases belong to no hidden value. */
    for (int o = 0; o < NET_OUT; o++) {
        int at = net_out_weight(o, NET_HIDDEN);
        child->g[at] = pick_parent(a, b, r)->g[at];
    }
    genome_mutate(child, r, s);
}

float genome_kinship(const Genome *a, const Genome *b)
{
    int matches = 0;
    for (int i = 0; i < GENOME_LEN; i++) {
        float d = fabsf(a->g[i] - b->g[i]);
        if (i == GENE_HUE && d > 0.5f) {
            d = 1.0f - d; /* 0.98 and 0.01 are close on the wheel */
        }
        if (d < KIN_TOLERANCE) {
            matches++;
        }
    }
    /* Counting is symmetric because fabsf(a - b) == fabsf(b - a) exactly:
     * a - b and b - a are the same number with opposite signs. */
    return (float)matches / (float)GENOME_LEN;
}

float genome_body_likeness(const Genome *a, const Genome *b)
{
    float total = 0.0f;
    for (int i = 0; i < GENE_TRAITS; i++) {
        float d = fabsf(a->g[i] - b->g[i]);
        if (i == GENE_HUE) {
            /* Round the wheel the short way, at most 0.5; doubled, so that
             * half way round counts as fully different, as 0 and 1 do. */
            d = (d > 0.5f ? 1.0f - d : d) * 2.0f;
        }
        total += 1.0f - d;
    }
    return total / (float)GENE_TRAITS;
}

/* The weights start after the traits: first every hidden value's NET_IN
 * + 1 weights, one row per hidden value, then every output's NET_HIDDEN +
 * 1 weights. A matrix stored row after row in a flat array, as the grid
 * is. */
int net_hidden_weight(int h, int i)
{
    return GENE_TRAITS + h * (NET_IN + 1) + i;
}

int net_out_weight(int o, int j)
{
    return GENE_TRAITS + NET_HIDDEN * (NET_IN + 1) + o * (NET_HIDDEN + 1) + j;
}
