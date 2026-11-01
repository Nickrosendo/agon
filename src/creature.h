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
    uint32_t lineage;   /* id of the founding ancestor */
    int x, y;
    uint8_t heading;    /* a HEADING_ value */
    float energy;
    uint32_t age;       /* ticks lived */
    Action current;     /* what the brain last said */
    uint16_t hold_left; /* ticks of current still to do */
} Creature;

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
} Pool;

/* Allocates capacity empty slots. False for a capacity below 1 or when
 * memory runs out; nothing to free then. */
bool pool_init(Pool *p, int capacity);
/* Empties every slot and starts ids from 0 again, without reallocating.
 * Does not touch any world: clear the world's occupant layer too. */
void pool_clear(Pool *p);
/* Frees the slots and empties *p; safe to call twice. */
void pool_free(Pool *p);
/* Places a new creature at (x, y), heading north, with energy 0 and age
 * 0. Returns its slot index, or -1 when the pool is full or the cell is
 * not free soil; nothing changes then. The caller sets the energy. */
int pool_spawn(Pool *p, World *w, int x, int y);
/* Removes the creature in slot idx from the world and frees the slot.
 * Does nothing to a slot that is already free. */
void pool_kill(Pool *p, World *w, int idx, const Settings *s);

/* Fills *out with what creature idx senses: its energy and age, and the
 * vegetation in the cell ahead, to its left and to its right. */
void creature_sense(const World *w, const Pool *p, int idx,
                    const Settings *s, Senses *out);
/* Carries out one tick of action a for creature idx. Returns the slot of
 * a child born by splitting, or -1 when none was. */
int creature_act(World *w, Pool *p, int idx, Action a, const Settings *s,
                 Rng *r);
/* Charges one tick of upkeep and ages the creature by one tick. Returns
 * false when its energy is gone: it must then be killed. */
bool creature_upkeep(Creature *c, const Settings *s);

#endif
