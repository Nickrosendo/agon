/*
 * creature: the pool that holds every creature, and what one creature
 * senses and does in a tick. Never touches the terminal and never calls
 * a brain: the simulation asks the brain and passes the answer here.
 */
#ifndef AGON_CREATURE_H
#define AGON_CREATURE_H

#include <stdbool.h>
#include <stdint.h>

#include "brain.h"
#include "genome.h"
#include "rng.h"
#include "settings.h"
#include "world.h"

/* Headings in clockwise order, so turning right is + 1 and turning left
 * is + 3 (that is, - 1), both modulo 4. */
enum { HEADING_NORTH, HEADING_EAST, HEADING_SOUTH, HEADING_WEST,
       HEADING_COUNT };

typedef struct {
    bool alive;
    int32_t next_free;  /* while dead: the next free slot, or -1 */
    uint32_t id;        /* unique for the whole run, never reused */
    /* The family name: a founder's own id, and for a child the name of one
     * parent, chosen at random, like a surname. Not a measure of relatedness:
     * with two parents, most of a child's genes can come from the other side. */
    uint32_t lineage;
    int x, y;
    uint8_t heading;    /* a HEADING_ value */
    float energy;
    uint32_t age;       /* ticks lived */
    Action current;     /* what the brain last said */
    uint16_t hold_left; /* ticks of current still to do */
    float move_points;  /* a move needs 1; gains speed per tick, up to 1 */
    uint16_t mate_wish; /* ticks left of wanting to mate; 0 when it does not */
    /* Food-chain level: 1 + the energy-weighted average level of what it
     * has eaten (plants are level 1, so a pure plant-eater is at 2). eaten
     * is the energy those meals gave, the weight of the average. Until
     * the first meal, level is LEVEL_START or the mean of its parents'. */
    float level;
    float eaten;
    /* Stored inside the creature, not behind a pointer: GENOME_LEN floats
     * (916 bytes) in every slot, allocated with the pool and never freed
     * on its own. */
    Genome genome;
} Creature;

/* The level of a creature that has eaten nothing yet and has no parent:
 * counted as a plant-eater. */
#define LEVEL_START 2.0f
/* From this level on, more than half of a creature's food was meat: a
 * meat-eater. The panel's bands and the plant-eater cap (with
 * cap_by_level) both use it. */
#define LEVEL_MEAT 2.5f

/* Below 0 and above 1 are not possible, but a sum of 0.25f four times is
 * exactly 1 while a sum of 0.1f ten times is 0.99999994. A move is
 * allowed from 1 - MOVE_SLACK move points, so rounding cannot cost a
 * creature a whole tick. */
#define MOVE_SLACK 1e-4f

/*
 * Every creature lives in one array of capacity slots, allocated once.
 * A dead slot is free for the next birth. The free slots form a linked
 * list threaded through the array itself: free_head is the first free
 * slot, its next_free the second, and so on, ending at -1. Finding a free
 * slot is then one step, not a search.
 */
typedef struct {
    Creature *items;
    int capacity, count; /* count: how many are alive */
    int32_t free_head;
    uint32_t next_id;
    /* Plant-eaters alive as counted by pool_count_plant_eaters, plus
     * those born since. Deaths are not taken off until the next count. */
    int plant_eaters;
    /* Births that could not happen because every slot was taken. The pool is
     * a safety limit and not a rule of the world, so this should stay 0: it
     * is counted so that reaching the limit is never quiet. */
    uint32_t births_refused;
    /* Whether this tick is in the mating season. Worked out from the tick by
     * the Sim before anyone acts, so it is neither saved nor hashed: the tick
     * is both. */
    bool in_season;
} Pool;

/* Allocates capacity empty slots. False for a capacity below 1 or when
 * memory runs out; nothing to free then. */
bool pool_init(Pool *p, int capacity);
/* Empties every slot and starts ids from 0 again, without reallocating.
 * Does not touch any world: clear the world's occupant layer too. */
void pool_clear(Pool *p);
/* Counts the living plant-eaters (creature_counts_as_plant_eater) into
 * p->plant_eaters. The simulation calls it once per tick, before anyone
 * acts; a birth checks the count against max_plant_eaters and adds its
 * child to it. */
void pool_count_plant_eaters(Pool *p, const Settings *s);
/* Whether c counts towards max_plant_eaters: by its food-chain level
 * when s->cap_by_level is set, else by its genes (not a hunter). */
bool creature_counts_as_plant_eater(const Creature *c, const Settings *s);
/* Frees the slots and empties *p; safe to call twice. */
void pool_free(Pool *p);
/* Places a new creature at (x, y), heading north, with energy 0, age 0
 * and an all-zero genome. Returns its slot index, or -1 when the pool is
 * full or the cell is not free soil; nothing changes then. The caller
 * sets the energy and the genome. */
int pool_spawn(Pool *p, World *w, int x, int y);
/* Removes the creature in slot idx from the world, leaves its body as
 * carrion (body_energy * size plus any energy left, at its level) and
 * frees the slot. Does nothing to a slot that is already free. */
void pool_kill(Pool *p, World *w, int idx, const Settings *s);

/* Fills *out with what creature idx senses: its energy, age, size, meat
 * digestion gene and whether it is the mating season; and along each of its three rays (ahead, left,
 * right) the vegetation and the scent of carrion in the first cell, and
 * the nearest creature within sight: its closeness, its size relative to
 * the viewer, its kinship, whether it is a hunter, whether it wishes to mate
 * and how alike its body looks. A ray stops at rock. */
void creature_sense(const World *w, const Pool *p, int idx,
                    const Settings *s, Senses *out);
/* What a bite by creature idx would do right now. Reads only, and uses no
 * random numbers, so the drawing side can ask too and show the same thing the
 * simulation does. cell is the target's cell (world_idx), -1 for
 * BITE_NOTHING; prey is the target's pool slot, for BITE_ATTACK only. */
enum { BITE_NOTHING, BITE_ATTACK, BITE_CARRION, BITE_PLANT };
typedef struct {
    int kind; /* a BITE_ value */
    int cell;
    int prey;
} BiteChoice;
BiteChoice creature_bite_choice(const World *w, const Pool *p, int idx,
                                const Settings *s);
/* Carries out one tick of action a for creature idx. Returns the slot of
 * a child born from it, or -1 when none was.
 *
 * ACT_MATE: an adult (see Settings: mate_age, and mate_threshold by size)
 * starts wishing to mate for mate_window ticks. If an adult beside it is
 * wishing too, and a cell beside either is free soil, a child is born there:
 * each parent gives mate_share of its energy, and the child's genome is
 * genome_cross of the two.
 *
 * In the mating season (p->in_season) every adult tries this first, whatever
 * its action: a birth takes up its tick; otherwise it goes on to act as usual.
 * Out of season only ACT_MATE tries. */
int creature_act(World *w, Pool *p, int idx, Action a, const Settings *s,
                 Rng *r);
/* Charges one tick of upkeep and ages the creature by one tick. Returns
 * false when its energy is gone or it has reached max_age: it must then
 * be killed. */
bool creature_upkeep(Creature *c, const Settings *s);

/* The body the genes describe (see Settings): size from size_min to
 * size_max, speed in move points per tick, sight in cells. */
float creature_size(const Creature *c, const Settings *s);
float creature_speed(const Creature *c, const Settings *s);
int creature_sight(const Creature *c, const Settings *s);
/* Energy one tick of life costs this creature. */
float creature_upkeep_cost(const Creature *c, const Settings *s);

/* True for a creature that digests meat better than plants. Only
 * hunters attack, and only plant-eaters are attacked. */
bool creature_is_hunter(const Creature *c);

/* Chance that an attacker of size a hits a target of size t:
 * a^k / (a^k + t^k). Equal sizes give 0.5; larger k makes size matter
 * more. A pure function (no state, no randomness), so tests can check it
 * exactly. */
float attack_chance(float a, float t, float k);

/* Records a meal that gave energy from a source at source_level (1 for
 * plants, the carrion's level for meat): updates level and eaten. Does
 * nothing for energy <= 0. Does not add the energy itself. */
void creature_ate(Creature *c, float energy, float source_level);

#endif
