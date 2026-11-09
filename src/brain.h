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

/* How many numbers a creature carries from one decision to the next. Without
 * them nothing a creature senses can last longer than a tick. */
#define MEMORY_VALUES 3

/* The three directions a creature looks in, relative to its heading. */
enum { RAY_AHEAD, RAY_LEFT, RAY_RIGHT, RAY_COUNT };

/* What each ray reports: the vegetation and the scent of carrion in the
 * first cell, the closeness of the nearest creature (1 next to the
 * viewer, falling with distance, 0 for none in sight), that creature's
 * size relative to the viewer's, its kinship, whether it is a hunter
 * (1) or not (0), whether it wishes to mate right now (1) or not (0), and
 * how alike its body looks (genome_body_likeness). */
enum {
    RAY_VEG,
    RAY_CARRION,
    RAY_NEAR,
    RAY_RELSIZE,
    RAY_KIN,
    RAY_HUNTER,
    RAY_READY,
    RAY_ALIKE,
    RAY_PLANT, /* closeness of the nearest plant in sight: 1 beside, 0 for none */
    /* Of the creature seen: 1 if it was hit in the last gang_window ticks; 1
     * minus the share of the 8 cells around it holding its own relatives (1 for
     * a stray); the share of its health it has lost. */
    RAY_FIGHTING,
    RAY_ALONE,
    RAY_HURT,
    RAY_VALUES
};

/* The senses, as positions in Senses.v. After the six single values come
 * the rays: ray k's value j is at SENSE_RAYS + k * RAY_VALUES + j. After the
 * rays come the drives, then the memory values, the last ones between -1 and 1.
 * The neural network has one input per sense, so its NET_IN must equal
 * SENSE_COUNT (56). An enum value may be set from earlier ones, and the next
 * goes on counting from there. */
enum {
    SENSE_ENERGY,
    SENSE_AGE,
    SENSE_SIZE,
    SENSE_DIET, /* the meat digestion gene */
    SENSE_SEASON, /* 1 in spring, the mating season, 0 outside it */
    /* The time of year as a point going round a circle once a year, each
     * coordinate moved from -1..1 into 0..1: 0.5 + 0.5 * sin and cos of
     * 2 pi * phase. Two numbers, because one alone reads the same at two times
     * of the year; together they tell every day apart, and the end of winter
     * runs smoothly into spring. */
    SENSE_YEAR_SIN,
    SENSE_YEAR_COS,
    /* The time of day, the same way: a point going round a circle once a day,
     * so dusk and dawn read differently although both are half light. */
    SENSE_DAY_SIN,
    SENSE_DAY_COS,
    SENSE_BLOCKED, /* 1 when a step ahead cannot succeed: water, rock or a creature */
    SENSE_RAYS,
    SENSE_HUNGER = SENSE_RAYS + RAY_COUNT * RAY_VALUES, /* 1 with no energy */
    SENSE_PAIN, /* the share of its health it has lost */
    SENSE_KIN_NEAR, /* the share of the 8 cells around holding relatives */
    SENSE_STRANGERS_NEAR, /* the share holding anyone else */
    /* 1 if a relative within KIN_NEAR cells (chronicle.h) was hit in the last
     * gang_window ticks: the rays tell where, this tells that. */
    SENSE_KIN_HIT,
    /* How recently it ate: 1 just after a meal, 0 after fed_fade_ticks or more
     * without one. A body sense, felt asleep too. Without it a creature biting
     * at nothing could tell only from energy falling slowly. */
    SENSE_FED,
    /* A fresh draw for each decision, written by the Sim and left 0 by
     * creature_sense, which the drawing side calls too and which must not
     * change the random generator. */
    SENSE_RANDOM,
    SENSE_MEMORY,
    SENSE_COUNT = SENSE_MEMORY + MEMORY_VALUES
};

enum {
    ACT_REST,
    ACT_TURN_LEFT,
    ACT_TURN_RIGHT,
    ACT_MOVE,
    ACT_BITE, /* eat the best food around; never hurts anyone (creature.c) */
    ACT_MATE, /* wish to mate, with anyone beside it who wishes too */
    ACT_ATTACK, /* attack whatever creature is on the cell ahead, of any kind */
    /* Deep sleep: upkeep falls to sleep_upkeep's share, the creature does
     * nothing and senses only its own body. It wakes by choosing anything else,
     * or when it is bitten (creature.c). */
    ACT_SLEEP,
    ACT_COUNT
};

/* Every sense is a number, mostly in 0..1. */
typedef struct {
    float v[SENSE_COUNT];
} Senses;

/* Senses that can be held at 0 for every brain, as blind_senses' bits: to test
 * whether a tactic really rests on them (the tactics design, section 11). */
enum {
    BLIND_DAY = 1 << 0,      /* SENSE_DAY_SIN and SENSE_DAY_COS */
    BLIND_FIGHTING = 1 << 1, /* RAY_FIGHTING on every ray */
    BLIND_ALONE = 1 << 2,    /* RAY_ALONE on every ray */
    BLIND_KIN_HIT = 1 << 3,  /* SENSE_KIN_HIT */
    BLIND_HEARING = 1 << 4,  /* the three hearing senses */
    BLIND_ALL = (1 << 5) - 1
};

/* An answer from a brain: what to do (an ACT_ value), for how many ticks to
 * keep doing it, and what to remember. A hold of 0 counts as 1. memory arrives
 * holding the creature's present values, so a brain that writes none leaves
 * them as they are. */
typedef struct {
    uint8_t kind;
    uint16_t hold;
    float memory[MEMORY_VALUES];
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
