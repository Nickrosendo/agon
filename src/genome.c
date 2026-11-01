#include "genome.h"

#include <math.h> /* fabsf, floorf, isfinite, powf */

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

void genome_copy_mutated(Genome *child, const Genome *parent, Rng *r,
                         const Settings *s)
{
    /* One assignment copies the whole struct, array included: here
     * GENOME_LEN * 4 bytes. (For a bare array, = would not compile.) The
     * copy comes first, so child and parent may even be the same genome. */
    *child = *parent;
    for (int i = 0; i < GENOME_LEN; i++) {
        /* One draw per value whether or not it mutates, so the number of
         * draws, and with it every later random number, does not depend
         * on which values changed. */
        if (rng_unit(r) >= s->mutation_rate) {
            continue;
        }
        float before = child->g[i];
        float step = rng_normal(r) * s->mutation_spread * gene_range(i, s);
        child->g[i] = gene_fix(i, before + step, before, s);
    }
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

/* powf(x, y) is x to the power y, in float. Costlier than a
 * multiplication (it goes through a logarithm and an exponential), which
 * matters only if called for every creature every tick; it is called per
 * bite. For x = 0 and y > 0 it returns exactly 0. */
float diet_plant_eff(float diet, float penalty)
{
    return powf(1.0f - diet, penalty);
}

float diet_meat_eff(float diet, float penalty)
{
    return powf(diet, penalty);
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
