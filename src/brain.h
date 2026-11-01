/*
 * brain: the one interface every creature decision goes through. A brain
 * is a struct of function pointers plus a pointer to its private state;
 * the simulation calls the functions without knowing which brain is
 * behind them. The whole world uses one brain at a time, chosen by name.
 */
#ifndef AGON_BRAIN_H
#define AGON_BRAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "settings.h"

/* The three directions a creature looks in, relative to its heading. */
enum { RAY_AHEAD, RAY_LEFT, RAY_RIGHT, RAY_COUNT };

/* What each ray reports: the vegetation in the first cell, the closeness
 * of the nearest carrion and of the nearest creature (1 next to the
 * viewer, falling with distance, 0 for none in sight), that creature's
 * size relative to the viewer's, and its kinship. */
enum { RAY_VEG, RAY_CARRION, RAY_NEAR, RAY_RELSIZE, RAY_KIN, RAY_VALUES };

/* The senses, as positions in Senses.v. After the four single values come
 * the rays: ray k's value j is at SENSE_RAYS + k * RAY_VALUES + j. The
 * neural network of lesson 6 has one input per sense, so its NET_IN must
 * equal SENSE_COUNT (19). */
enum {
    SENSE_ENERGY,
    SENSE_AGE,
    SENSE_SIZE,
    SENSE_DIET, /* the meat digestion gene */
    SENSE_RAYS,
    SENSE_COUNT = SENSE_RAYS + RAY_COUNT * RAY_VALUES
};

enum {
    ACT_REST,
    ACT_TURN_LEFT,
    ACT_TURN_RIGHT,
    ACT_MOVE,
    ACT_BITE,
    ACT_SPLIT,
    ACT_COUNT
};

/* Every sense is a number, mostly in 0..1. */
typedef struct {
    float v[SENSE_COUNT];
} Senses;

/* An answer from a brain: what to do (an ACT_ value) and for how many
 * ticks to keep doing it. A hold of 0 counts as 1. */
typedef struct {
    uint8_t kind;
    uint16_t hold;
} Action;

/* An incomplete type: the compiler learns that a struct Genome exists,
 * but not what is inside it (genome.h says). A file that includes only
 * this header may pass Genome pointers around but cannot look inside or
 * make a Genome; brain_net.c includes genome.h as well, and can. */
typedef struct Genome Genome;

typedef struct Brain Brain;

/*
 * A brain. The fields are function pointers: variables that hold the
 * address of a function, called like the function itself. Every brain
 * fills in all five. state is the brain's own data, kept behind a
 * void * (a pointer to "something"); only the brain's own functions know
 * what it really points to.
 */
struct Brain {
    const char *name;
    void *state;
    /* Sets up state. False if that failed; nothing to destroy then. */
    bool (*create)(Brain *b, const Settings *s, uint64_t seed);
    /* Releases whatever create set up. */
    void (*destroy)(Brain *b);
    /* Fills *out for creature id, given its senses, and returns true; or
     * returns false at once for "not ready", leaving *out alone. Never
     * waits. */
    bool (*decide)(Brain *b, uint32_t id, const Genome *g, const Senses *in,
                   Action *out);
    /* A creature was born or placed in the world. */
    void (*on_birth)(Brain *b, uint32_t id, const Genome *g);
    /* A creature died; per-creature state can go. */
    void (*on_death)(Brain *b, uint32_t id);
};

/* The built-in brains. Templates: name and functions filled in, state
 * NULL. brain_open copies one and creates it. The random walker is in
 * brain_random.c, the neural network in brain_net.c. */
extern const Brain BRAIN_RANDOM;
extern const Brain BRAIN_NET;

/* Finds the brain called name among the registered templates, copies it
 * into *b and creates it with the given seed. False for an unknown name
 * or a failed create; *b is then empty, and closing it is harmless. */
bool brain_open(Brain *b, const char *name, const Settings *s, uint64_t seed);
/* Destroys the brain and empties *b; safe to call twice. */
void brain_close(Brain *b);
/* Adds a template to the registry, so brain_open can find it by name.
 * The template must stay alive for the whole run. False when the
 * registry is full. The built-in brains are registered from the start;
 * tests use this for their scripted brain. */
bool brain_register(const Brain *tmpl);

#endif
