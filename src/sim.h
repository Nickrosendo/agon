/*
 * sim: the whole simulation in one struct, advanced one tick at a time
 * in a fixed order. Never touches the terminal.
 */
#ifndef AGON_SIM_H
#define AGON_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "rng.h"
#include "settings.h"
#include "world.h"

/*
 * A Sim owns its world: the World struct sits inside the Sim (not behind
 * a pointer), and the layers the World points to are freed by sim_free.
 * The creature pool, brain and statistics join in later lessons.
 */
typedef struct {
    Settings set; /* a copy, so the caller's Settings can go away */
    World world;
    Rng rng;
    uint64_t seed, tick;
} Sim;

/* Allocates a w by h world and builds it from seed. False if the size is
 * invalid or memory runs out; nothing to free then. */
bool sim_init(Sim *s, const Settings *set, uint64_t seed, int w, int h);
/* Rebuilds the same-sized world from a new seed, reusing its memory. */
void sim_reseed(Sim *s, uint64_t seed);
void sim_step(Sim *s);
void sim_free(Sim *s);
/* A fingerprint of the whole state: equal for equal states, and almost
 * certainly different otherwise. */
uint64_t sim_checksum(const Sim *s);

#endif
