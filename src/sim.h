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
#include "stats.h"
#include "world.h"

/* One creature's place in the shuffled order of a tick: its slot and the
 * id it had when the order was made. If the slot holds someone else by
 * the time its turn comes, the ids differ and the turn is skipped. */
typedef struct {
    int32_t idx;
    uint32_t id;
} Turn;

/*
 * What the game over screen tells: the most alive at once, and the oldest and
 * largest creatures that ever lived. These cannot be worked out afterwards, so
 * unlike stats they are part of the world's state: saved and fingerprinted.
 */
typedef struct {
    int peak_population;
    uint32_t oldest_id, oldest_age;
    uint32_t largest_id;
    float largest_size;
    uint64_t ended_at; /* the tick the last creature died; 0 while any live */
} WorldRecords;

/*
 * A Sim owns its world, its creature pool and its brain: the structs sit
 * inside the Sim (not behind pointers), and the memory they point to is
 * freed by sim_free. stats is the latest sample (see stats.h), taken every
 * stats_every ticks.
 */
typedef struct {
    Settings set; /* a copy, so the caller's Settings can go away */
    World world;
    Pool pool;
    Brain brain;
    Turn *turns;  /* pool capacity entries, reused every tick */
    Stats stats;
    uint32_t *scratch; /* pool capacity entries, for stats_sample */
    Rng rng;
    uint64_t seed, tick;
    WorldRecords records;
} Sim;

/* Allocates a w by h world and a creature pool, opens the brain named in
 * set and builds the world from seed. False if the size or the pool
 * capacity is invalid, the brain name unknown, or memory runs out;
 * nothing to free then. */
bool sim_init(Sim *s, const Settings *set, uint64_t seed, int w, int h);
/* The first half of sim_init: allocates a w by h world and the pool, and
 * builds nothing. False as for sim_init, with nothing to free. */
bool sim_alloc(Sim *s, const Settings *set, int w, int h);
/* After a Sim allocated by sim_alloc was filled in from a save file: opens
 * the brain, tells it about every living creature, and takes the numbers
 * for the panel without adding to the histories. A brain's private state
 * is not saved, so a brain that has any starts it afresh; the "net" brain
 * has none. False for an unknown brain; the Sim must then be freed. */
bool sim_resume(Sim *s);
/* Rebuilds the same-sized world from a new seed, reusing its memory. The
 * brain is closed and opened again with the new seed; false if that
 * fails, and the Sim must then be freed. */
bool sim_reseed(Sim *s, uint64_t seed);
void sim_step(Sim *s);
/* True when no creature is alive. Nothing brings one back by default, so the
 * programs stop stepping such a world and show its game over screen. Stepping it
 * anyway is safe: its plants would carry on. */
bool sim_is_over(const Sim *s);
/* Whether a tick falls in the mating season. Pure arithmetic on the tick, so
 * the drawing side can ask too without touching the world. */
bool sim_in_season(uint64_t tick, const Settings *set);
void sim_free(Sim *s);
/* A fingerprint of the whole state: equal for equal states, and almost
 * certainly different otherwise. */
uint64_t sim_checksum(const Sim *s);

#endif
