/*
 * sim: the whole simulation in one struct, advanced one tick at a time
 * in a fixed order. Never touches the terminal.
 */
#ifndef AGON_SIM_H
#define AGON_SIM_H

#include <stdbool.h>
#include <stdint.h>

#include "brain.h"
#include "creature.h"
#include "rng.h"
#include "settings.h"
#include "world.h"

/* One creature's place in the shuffled order of a tick: its slot and the
 * id it had when the order was made. If the slot holds someone else by
 * the time its turn comes, the ids differ and the turn is skipped. */
typedef struct {
    int32_t idx;
    uint32_t id;
} Turn;

/*
 * A Sim owns its world, its creature pool and its brain: the structs sit
 * inside the Sim (not behind pointers), and the memory they point to is
 * freed by sim_free. Statistics join in lesson 8.
 */
typedef struct {
    Settings set; /* a copy, so the caller's Settings can go away */
    World world;
    Pool pool;
    Brain brain;
    Turn *turns;  /* pool capacity entries, reused every tick */
    Rng rng;
    uint64_t seed, tick;
} Sim;

/* Allocates a w by h world and a creature pool, opens the brain named in
 * set and builds the world from seed. False if the size or the pool
 * capacity is invalid, the brain name unknown, or memory runs out;
 * nothing to free then. */
bool sim_init(Sim *s, const Settings *set, uint64_t seed, int w, int h);
/* Rebuilds the same-sized world from a new seed, reusing its memory. The
 * brain is closed and opened again with the new seed; false if that
 * fails, and the Sim must then be freed. */
bool sim_reseed(Sim *s, uint64_t seed);
void sim_step(Sim *s);
void sim_free(Sim *s);
/* A fingerprint of the whole state: equal for equal states, and almost
 * certainly different otherwise. */
uint64_t sim_checksum(const Sim *s);

#endif
