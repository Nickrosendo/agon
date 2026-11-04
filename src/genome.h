/*
 * genome: everything a creature inherits, as one flat array of floats.
 * The first GENE_TRAITS values are traits (size, speed, sight, leaf, fruit and
 * meat digestion, hue, ageing, armour, weapon, growth, territory);
 * the rest are the weights of the creature's neural network. A child takes
 * each part from one of its two parents, with small random changes.
 */
#ifndef AGON_GENOME_H
#define AGON_GENOME_H

#include "rng.h"
#include "settings.h"

/* The traits, as positions in Genome.g. Each is stored in 0..1.
 * GENE_LEAF, GENE_FRUIT and GENE_MEAT are how well the creature digests each
 * food; they are separate genes, so one can rise without the others falling,
 * and an upkeep cost (see Settings) keeps a creature from having both. */
enum {
    GENE_SIZE,
    GENE_SPEED,
    GENE_SIGHT,
    GENE_LEAF,
    GENE_FRUIT,
    GENE_MEAT,
    GENE_HUE,
    GENE_AGEING, /* 0 ages slowly, 1 ages quickly (see Settings) */
    GENE_ARMOUR, /* takes off a share of every hit; costs upkeep */
    GENE_WEAPON, /* adds to every hit landed; costs upkeep */
    GENE_GROWTH, /* the share of surplus energy that goes into body */
    GENE_TERRITORY, /* how readily an adult turns on a stranger beside it */
    GENE_TRAITS
};

/* The network: NET_IN senses plus a constant 1 feed NET_HIDDEN hidden
 * values, which plus a constant 1 feed NET_OUT outputs, one per action and
 * one per memory value. NET_IN must equal SENSE_COUNT and NET_OUT
 * ACT_COUNT + MEMORY_VALUES (brain.h); brain_net.c checks both when compiled. */
#define NET_IN 41
#define NET_HIDDEN 6
#define NET_OUT 10
/* The parentheses matter: a macro is pasted as text, and without them
 * "2 * NET_WEIGHTS" would multiply only the first term. */
#define NET_WEIGHTS ((NET_IN + 1) * NET_HIDDEN + (NET_HIDDEN + 1) * NET_OUT)
#define GENOME_LEN (GENE_TRAITS + NET_WEIGHTS)

/*
 * The definition brain.h left out. brain.h's "typedef struct Genome
 * Genome;" said only that a struct Genome exists; this says what is in it.
 * Repeating the typedef is allowed (since C11) because it names the same
 * type. A fixed-size array inside a struct is part of the struct: copying
 * a Genome by assignment copies all GENOME_LEN floats.
 */
typedef struct Genome {
    float g[GENOME_LEN];
} Genome;

/* Random traits in 0..1 and random weights, normally spread with spread
 * weight_start_spread and clamped to plus or minus weight_limit. */
void genome_random(Genome *g, Rng *r, const Settings *s);
/* Writes into *child a mix of two parents. Each trait comes whole from one
 * of them; so does each hidden value, with every weight into it and out of
 * it, because a hidden value only means something together with its own
 * weights, and mixing single weights tends to break both networks; and each
 * output's bias. Then each value, with probability mutation_rate, moves by a
 * normal step of mutation_spread times the value's range. Traits are clamped
 * to 0..1, hue wraps round, weights are clamped to plus or minus
 * weight_limit. Crossing a genome with itself gives a mutated copy. */
void genome_cross(Genome *child, const Genome *a, const Genome *b, Rng *r,
                  const Settings *s);
/* How related two genomes look: the share of values that nearly match,
 * from 1 (identical) down towards 0. The same either way round. */
float genome_kinship(const Genome *a, const Genome *b);
/* How alike two bodies look: 1 for identical traits, falling to 0 for opposite
 * ones. Only the traits count, never the network weights. Hue is a distance
 * round the colour wheel. The same either way round. */
float genome_body_likeness(const Genome *a, const Genome *b);

/* Where a weight sits in Genome.g. From input i (NET_IN for the constant
 * 1, the bias) into hidden value h; from hidden value j (NET_HIDDEN for
 * the bias) into output o. */
int net_hidden_weight(int h, int i);
int net_out_weight(int o, int j);

#endif
