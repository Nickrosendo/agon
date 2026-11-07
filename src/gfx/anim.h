/*
 * anim: what the drawing side remembers about each creature, so movement
 * can be shown smoothly. Reads the simulation and writes only its own
 * memory, so it cannot change what the simulation does.
 */
#ifndef AGON_ANIM_H
#define AGON_ANIM_H

#include <stdbool.h>
#include <stdint.h>

#include "sim.h"

/* How long a newborn takes to grow to full size on screen, in ticks. */
#define ANIM_POP_TICKS 8
/* Above this many ticks per second, creatures jump instead of gliding. */
#define ANIM_GLIDE_MAX_SPEED 15
/* A move that takes at most this many ticks is shown as a run. */
#define ANIM_RUN_TICKS 1.5f

/* What a fighter's mouth is doing. The rules have no such bite, so this
 * value belongs to the drawing side, after the rules' own values. */
enum { POSE_ATTACK = BITE_PLANT + 1 };

/* Where a slot's attack is: none, playing, or playing out after the
 * simulation stopped it (see anim_update). */
enum { FIGHT_NONE, FIGHT_ON, FIGHT_ENDING };
/* An ending fight's impact plays this many times as fast as a fight's, and
 * its release this many times: together they are over within 6 frames. */
#define ANIM_ENDING_IMPACT_RATE 2.0f
#define ANIM_ENDING_RELEASE_RATE 4.0f

/* One pool slot's memory. */
typedef struct {
    uint32_t id;
    bool seen;            /* false until this slot's creature was looked at */
    int from_x, from_y;   /* the cell it last moved from */
    int x, y;             /* the cell it was on when last looked at */
    uint64_t moved_tick;  /* the tick it was first seen on x, y */
    uint64_t born_tick;
    float move_ticks;     /* how many ticks that move should take to show */
    int8_t face;          /* +1 facing right, -1 facing left */
    /* Its attack, when it fights: how far along (0..1, the share of
     * POSE_ATTACK_SECONDS), the body time it was last moved on at, and whom
     * it attacks, by slot and by id, since a slot can be reused. */
    uint8_t fight;        /* FIGHT_NONE, FIGHT_ON, FIGHT_ENDING */
    float fight_c;
    uint64_t fight_ms;
    int32_t victim_slot;
    uint32_t victim_id;
    int32_t hit_by;       /* the slot attacking this one this frame, or -1 */
} AnimSlot;

typedef struct {
    AnimSlot *slots;
    int capacity;
    uint64_t last_tick;
    bool started;         /* false until the first anim_update */
    bool jump;            /* no gliding this frame: see ANIM_GLIDE_MAX_SPEED */
    int ticks_per_second; /* as last given to anim_update: how fast a glide goes */
} Anim;

/* How to draw one creature right now. */
typedef struct {
    float x, y;  /* in cells, possibly between two */
    int action;  /* an ACTION_ value (skeleton.h): which cycle */
    int frame;   /* the frame of that cycle */
    float eyes;  /* 1 open, 0.4 half shut, 0 shut: blinks are drawn, not pictured */
    bool flip;   /* true when facing left */
    float pop;   /* 0..1; 1 is full size */
    int bite;    /* a BITE_ value (creature.h), or POSE_ATTACK: what its mouth is doing */
    int target_x, target_y; /* the cell it is biting, unless BITE_NOTHING */
    /* Being hit: how far its colours go to white (0..1), and how far it is
     * pushed from its cell, away from its attacker, in cells: knock_x the
     * push, tremble the shake this moment, both signed (+ east). */
    float flash, knock_x, tremble;
} AnimPose;

/* False when memory runs out; nothing to free then. */
bool anim_init(Anim *a, int capacity);
/* Frees the memory and empties *a; safe to call twice. */
void anim_free(Anim *a);
/* Forgets everything: after r, or a load, when ids start again. */
void anim_reset(Anim *a);
/*
 * Looks at every creature. Once per frame, after the frame's ticks; body_ms
 * is the body clock, which stops while paused, and moves fights on.
 *
 * A fight is on while the simulation says the creature attacks and the
 * creature it attacks is the one it started on (same slot, same id). Its
 * share fight_c runs through POSE_ATTACK_SECONDS; a long fight goes from the
 * end of the hold (0.8) back to its start (0.56), so it holds and shakes.
 * Above ANIM_GLIDE_MAX_SPEED ticks a second it starts at the pounce (0.35),
 * skips the hold and pounces again. When the simulation stops it, the fight
 * jumps ahead to the bite (0.5) or the release (0.8) and plays out fast.
 */
void anim_update(Anim *a, const Sim *s, int ticks_per_second, uint64_t body_ms);
/* The pose of the living creature in pool slot `slot`. tick_fraction (0..1)
 * is how far the frame is towards the next tick; now_ms is the body clock,
 * which stops while paused, and picks the frame of every cycle and the blinks.
 *
 * The action: asleep, sleep; fighting (a fight on or ending), attack, its
 * frame from fight_c; gliding to a cell, run when the move takes at most
 * ANIM_RUN_TICKS ticks, else walk; biting leaves, graze; biting fruit or
 * carrion, bite; anything else, rest. A creature being hit flashes and is
 * pushed (flash, knock_x, tremble), whatever it is doing. */
void anim_pose(const Anim *a, const Sim *s, int slot, float tick_fraction,
               uint64_t now_ms, AnimPose *out);

#endif
