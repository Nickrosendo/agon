/*
 * inspect: the cursor of inspect mode. It sits on a world cell; when that
 * cell holds a creature, the cursor follows that creature as it moves,
 * until it dies. Only state and rules here: drawing is in render.c (the
 * cursor) and panel.c (what is under it), keys in main.c.
 */
#ifndef AGON_INSPECT_H
#define AGON_INSPECT_H

#include <stdbool.h>
#include <stdint.h>

#include "sim.h"

typedef struct {
    bool on;
    int x, y;       /* the world cell under the cursor */
    bool following; /* true while a living creature is under it */
    uint32_t id;    /* that creature's id, while following */
} Inspect;

/* Turns inspect mode on, with the cursor on the oldest living creature
 * whose cell is inside 0 .. max_x - 1, 0 .. max_y - 1 (the part of the
 * world on screen); with none there, on the middle of that area. */
void inspect_start(Inspect *in, const Sim *s, int max_x, int max_y);

/* Moves the cursor by (dx, dy) cells, stopping at the edges of the area
 * 0 .. max_x - 1, 0 .. max_y - 1, and follows whatever creature is on the
 * new cell (or none). */
void inspect_move(Inspect *in, const Sim *s, int dx, int dy, int max_x,
                  int max_y);

/* Call once per frame: puts the cursor on the followed creature's new
 * cell, or stops following when it has died. The cursor then stays
 * where the creature was last seen. */
void inspect_update(Inspect *in, const Sim *s);

/* Turns inspect mode off and follows nobody. Needed when a new world starts:
 * ids begin again from 0, so a remembered id could match a stranger. */
void inspect_stop(Inspect *in);

/* Follows the creature in this slot, which must be alive, wherever it is. */
void inspect_follow(Inspect *in, const Sim *s, int slot);

/* Who to follow from the panel: a living creature's slot, or -1 when there is
 * none. On a tie, the lowest slot. Highest on the food chain, so there is always
 * an answer while anyone lives, even with no top hunter at this moment. */
int inspect_highest_level(const Sim *s);
int inspect_oldest(const Sim *s);
/* The biggest body in this family. */
int inspect_largest_of_family(const Sim *s, uint32_t lineage);

/* The creature being followed, or NULL when none is. */
const Creature *inspect_creature(const Inspect *in, const Sim *s);

#endif
