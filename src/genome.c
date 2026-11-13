#include "genome.h"

#include <assert.h> /* static_assert */
#include <math.h>   /* fabsf, floorf, isfinite */
#include <stdbool.h>

#include "brain.h" /* the senses and actions the instinct is made of */

/* Two values "nearly match" for genome_kinship when they differ by less
 * than this. Random genomes rarely match (about 1 value in 20); a child
 * keeps every value it did not mutate. */
#define KIN_TOLERANCE 0.05f
/* GENOME_LEN rounded down to a multiple of 8. At -O2, GCC only runs a loop
 * several values at a time when the count divides evenly into its blocks, so
 * genome_kinship counts this many in one loop and the few left in another.
 * ~7 is the bits 0b...11111000: & with it clears the last three bits. */
#define KIN_BLOCKED (GENOME_LEN & ~7)

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

/* The recipe, one row per weight: into hidden value h from sense i (NET_IN for
 * the hidden value's bias), or from hidden value j (NET_HIDDEN for the output's
 * bias) into output o. The four hidden values mean, roughly:
 *   0 "food within a bite": +0.96 with it, -0.96 without;
 *   1 "the way to turn": plants to the left minus the right, and less strongly
 *     a remembered meal to the left minus the right;
 *   2 "the way ahead is good": plants or a remembered meal ahead, not blocked;
 *   3 "hungry, and nothing in sight or in mind": hunger, less everything seen,
 *     in reach or remembered.
 * Worked through by hand in the study doc of 2026-11-12; checked by
 * test_instinct_habit (tests/test_brain.c). */
typedef struct {
    bool into_hidden; /* true: sense i into hidden h; false: hidden j into output o */
    int to, from;     /* h and i, or o and j */
    float value;
} InstinctWeight;

/* The plant value of view v, as a sense position (brain.h). */
#define PLANT_IN(v) (SENSE_VIEWS + (v) * VIEW_VALUES + VIEW_PLANT)

static const InstinctWeight INSTINCT[] = {
    /* hidden 0: food within a bite */
    {true, 0, SENSE_REACH_FOOD, 4.0f},
    {true, 0, NET_IN, -2.0f},
    /* hidden 1: the way to turn, + for left */
    {true, 1, PLANT_IN(VIEW_LEFT), 4.0f},
    {true, 1, PLANT_IN(VIEW_RIGHT), -4.0f},
    {true, 1, SENSE_PLACE_LEFT, 2.0f},
    {true, 1, SENSE_PLACE_RIGHT, -2.0f},
    /* hidden 2: the way ahead is good */
    {true, 2, PLANT_IN(VIEW_AHEAD), 4.0f},
    {true, 2, SENSE_PLACE_AHEAD, 2.0f},
    {true, 2, SENSE_BLOCKED, -4.0f},
    {true, 2, NET_IN, -0.5f},
    /* hidden 3: hungry with nothing in sight or in mind */
    {true, 3, SENSE_HUNGER, 3.0f},
    {true, 3, PLANT_IN(VIEW_AHEAD), -4.0f},
    {true, 3, PLANT_IN(VIEW_LEFT), -4.0f},
    {true, 3, PLANT_IN(VIEW_RIGHT), -4.0f},
    {true, 3, SENSE_REACH_FOOD, -4.0f},
    {true, 3, SENSE_PLACE_LEFT, -2.0f},
    {true, 3, SENSE_PLACE_AHEAD, -2.0f},
    {true, 3, SENSE_PLACE_RIGHT, -2.0f},
    {true, 3, NET_IN, -1.5f},
    /* the outputs */
    {false, ACT_BITE, 0, 4.0f},
    {false, ACT_BITE, NET_HIDDEN, 1.0f},
    {false, ACT_MOVE, 0, -2.0f},
    {false, ACT_MOVE, 2, 3.0f},
    {false, ACT_MOVE, NET_HIDDEN, 1.0f},
    {false, ACT_TURN_LEFT, 1, 3.0f},
    {false, ACT_TURN_LEFT, NET_HIDDEN, -0.5f},
    {false, ACT_TURN_RIGHT, 1, -3.0f},
    /* 1.5 * hidden 3 + 1.5 is 0 when hidden 3 is at its floor of -1 and up to
     * 3 when it is at +1: looking round only ever adds to turning right. With
     * a plain 3 * hidden 3 it would also take away from it, and a creature
     * seeing food to its right would turn away from it. */
    {false, ACT_TURN_RIGHT, 3, 1.5f},
    {false, ACT_TURN_RIGHT, NET_HIDDEN, 1.0f},
};

/* sizeof an array divided by the size of one element: how many it holds. It
 * works only on the array itself, where the compiler knows its size; inside a
 * function that was passed it, it would be a pointer, and the sum would be
 * wrong without any warning. */
#define INSTINCT_COUNT ((int)(sizeof(INSTINCT) / sizeof(INSTINCT[0])))
static_assert(sizeof(INSTINCT) / sizeof(INSTINCT[0]) <= INSTINCT_MAX,
              "INSTINCT_MAX holds the whole recipe");

static int instinct_index(const InstinctWeight *w)
{
    return w->into_hidden ? net_hidden_weight(w->to, w->from)
                          : net_out_weight(w->to, w->from);
}

int genome_instinct(int *index, float *value, int max)
{
    int n = INSTINCT_COUNT < max ? INSTINCT_COUNT : max;
    for (int k = 0; k < n; k++) {
        index[k] = instinct_index(&INSTINCT[k]);
        value[k] = INSTINCT[k].value;
    }
    return n;
}

void genome_add_instinct(Genome *g, const Settings *s)
{
    for (int k = 0; k < INSTINCT_COUNT; k++) {
        int at = instinct_index(&INSTINCT[k]);
        g->g[at] = clamp(g->g[at] + INSTINCT[k].value, -s->weight_limit,
                         s->weight_limit);
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
        if (i < GENE_TRAITS) {
            step *= s->trait_mutation_scale; /* times 1.0f changes no bit */
            /* 1u << i: an unsigned one moved up i places, the bit for gene
             * i. & keeps only the bits set in both, so the result is not 0
             * exactly when gene i's bit is on. */
            bool may_jump = ((unsigned)s->jump_genes & (1u << i)) != 0;
            /* && stops at a false left side, so with the bit off the right
             * side's draw never happens: defaults draw what they always did. */
            if (may_jump && rng_unit(r) < s->jump_chance) {
                g->g[i] = rng_unit(r);
                continue;
            }
        }
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
    /* Every gene counted the plain way first, with no test of which gene it
     * is inside the loop: a loop with no branch is one the compiler can run
     * several genes at a time (SSE instructions). This is the hottest loop of
     * a crowded world, called for every neighbour every tick. */
    int matches = 0;
    for (int i = 0; i < KIN_BLOCKED; i++) {
        /* A comparison is an int in C, 1 or 0, so it adds like a count. */
        matches += fabsf(a->g[i] - b->g[i]) < KIN_TOLERANCE;
    }
    for (int i = KIN_BLOCKED; i < GENOME_LEN; i++) {
        matches += fabsf(a->g[i] - b->g[i]) < KIN_TOLERANCE;
    }
    /* Then the one gene on a wheel is put right: 0.98 and 0.01 are close.
     * Take back the plain count of it and add the count round the wheel. */
    float d = fabsf(a->g[GENE_HUE] - b->g[GENE_HUE]);
    float round = d > 0.5f ? 1.0f - d : d;
    matches += (round < KIN_TOLERANCE) - (d < KIN_TOLERANCE);
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
