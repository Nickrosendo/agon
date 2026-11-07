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

/* One pool slot's memory. */
/* Fighting looks like biting, with a slash for a mark. The rules have no such
 * bite, so this value belongs to the drawing side, after the rules' own values. */
enum { POSE_ATTACK = BITE_PLANT + 1 };

typedef struct {
    uint32_t id;
    bool seen;            /* false until this slot's creature was looked at */
    int from_x, from_y;   /* the cell it last moved from */
    int x, y;             /* the cell it was on when last looked at */
    uint64_t moved_tick;  /* the tick it was first seen on x, y */
    uint64_t born_tick;
    float move_ticks;     /* how many ticks that move should take to show */
    int8_t face;          /* +1 facing right, -1 facing left */
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
} AnimPose;

/* False when memory runs out; nothing to free then. */
bool anim_init(Anim *a, int capacity);
/* Frees the memory and empties *a; safe to call twice. */
void anim_free(Anim *a);
/* Forgets everything: after r, or a load, when ids start again. */
void anim_reset(Anim *a);
/* Looks at every creature. Once per frame, after the frame's ticks. */
void anim_update(Anim *a, const Sim *s, int ticks_per_second);
/* The pose of the living creature in pool slot `slot`. tick_fraction (0..1)
 * is how far the frame is towards the next tick; now_ms is the body clock,
 * which stops while paused, and picks the frame of every cycle and the blinks.
 *
 * The action: asleep, sleep; gliding to a cell, run when the move takes at
 * most ANIM_RUN_TICKS ticks, else walk; biting leaves, graze; biting fruit or
 * carrion, or fighting, bite; anything else, rest. */
void anim_pose(const Anim *a, const Sim *s, int slot, float tick_fraction,
               uint64_t now_ms, AnimPose *out);

#endif
