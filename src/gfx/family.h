/*
 * family: what each family's body has looked like over time. The simulation
 * keeps a founder's id (lineage) on each creature but no record of past bodies,
 * and must not start keeping one, so the drawing side does. It reads the
 * simulation and writes only its own memory. It is not saved: after a load, a
 * family's record starts afresh.
 */
#ifndef AGON_FAMILY_H
#define AGON_FAMILY_H

#include <stdbool.h>
#include <stdint.h>

#include "sim.h"
#include "sprite.h"

#define FAMILY_MAX 32       /* how many families are remembered */
#define FAMILY_SAMPLES 32   /* how many samples each one keeps */
#define FAMILY_STRIP 8      /* how many bodies a strip shows */
#define FAMILY_PASS_TICKS 500 /* how often the world is looked at */
/* family_largest's answer when there is no family. */
#define FAMILY_NONE UINT32_MAX

/* One family's average at one moment. body_size is the members' mean real
 * size (creature_size: gene and growth), which picks the species and the
 * picture's size level as it does for a living creature. */
typedef struct {
    BodyTraits traits;
    float hue, meat_share, body_size;
} FamilySample;

/*
 * count: how many were alive at the last pass. n: samples held, oldest first.
 * every: a sample is taken on passes divisible by this. It doubles each time the
 * samples fill up, so they always span the family's whole recorded life.
 */
typedef struct {
    uint32_t lineage;
    int count, n, every;
    FamilySample samples[FAMILY_SAMPLES];
} FamilyRecord;

typedef struct {
    FamilyRecord records[FAMILY_MAX]; /* inside the struct: about 30 KB */
    int used;
    uint64_t passes, next_tick;
    uint32_t *scratch;
    int scratch_len;
} Families;

/* False when memory runs out; nothing to free then. */
bool family_init(Families *f, int pool_capacity);
/* Frees the memory and empties *f; safe to call twice. */
void family_free(Families *f);
/* Forgets everything: after r, when ids start again. */
void family_reset(Families *f);
/* Once per frame. At most one pass per call, however many ticks went by. */
void family_update(Families *f, const Sim *s);
/* The recorded family with the most members, or FAMILY_NONE. */
uint32_t family_largest(const Families *f, const Sim *s);
/* Up to FAMILY_STRIP evenly spaced samples of this family, oldest first and
 * always with its first and last. Returns how many it wrote: 0 for a family
 * with no record. */
int family_strip(const Families *f, uint32_t lineage,
                 FamilySample out[FAMILY_STRIP]);

#endif
