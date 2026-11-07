#include "anim.h"

#include <stdlib.h> /* calloc, free */
#include <string.h> /* memset */

#include "pose.h"     /* pose_frame, pose_cycle_seconds, pose_cadence, pose_eyes */
#include "sprite.h"   /* sprite_body; the ACTION_ values */

bool anim_init(Anim *a, int capacity)
{
    /* calloc: all zeros, so every slot starts with seen false. */
    AnimSlot *slots = calloc((size_t)capacity, sizeof(*slots));
    *a = (Anim){.slots = slots, .capacity = slots != NULL ? capacity : 0};
    return slots != NULL;
}

void anim_free(Anim *a)
{
    free(a->slots);
    *a = (Anim){0};
}

void anim_reset(Anim *a)
{
    /* sizeof(*a->slots) is one slot: sizeof(a->slots) would be the size of
     * the pointer, and would clear only part of the first slot. */
    memset(a->slots, 0, (size_t)a->capacity * sizeof(*a->slots));
    a->started = false;
    a->jump = false;
}

/* The shortest signed step from a to b on a wrapped axis of this size. */
static int wrapped_step(int a, int b, int size)
{
    int d = b - a;
    if (d > size / 2) {
        d -= size;
    } else if (d < -size / 2) {
        d += size;
    }
    return d;
}

/* First sight of a creature: no glide, whatever its position. An old
 * creature (after a load) counts as born long ago, so it does not pop. */
static void first_sight(AnimSlot *m, const Creature *c, uint64_t tick)
{
    uint64_t age = c->age;
    /* Unsigned subtraction below zero wraps to a huge number. */
    m->born_tick = age < tick ? tick - age : 0;
    m->id = c->id;
    m->seen = true;
    m->from_x = m->x = c->x;
    m->from_y = m->y = c->y;
    m->moved_tick = tick;
    m->move_ticks = 1.0f;
    m->face = 1;
}

static void update_slot(AnimSlot *m, const Creature *c, const Sim *s)
{
    if (!c->alive) {
        m->seen = false;
        return;
    }
    if (!m->seen || m->id != c->id) {
        first_sight(m, c, s->tick);
    } else if (c->x != m->x || c->y != m->y) {
        m->from_x = m->x;
        m->from_y = m->y;
        m->x = c->x;
        m->y = c->y;
        m->moved_tick = s->tick;
        float speed = creature_speed(c, &s->set);
        float ticks = speed > 0.0f ? 1.0f / speed : 1.0f;
        m->move_ticks = ticks < 1.0f ? 1.0f : ticks;
    }
    /* North and south keep the last horizontal facing. */
    if (c->heading == HEADING_EAST) {
        m->face = 1;
    } else if (c->heading == HEADING_WEST) {
        m->face = -1;
    }
}

void anim_update(Anim *a, const Sim *s, int ticks_per_second)
{
    bool many_ticks = a->started && s->tick - a->last_tick > 1;
    a->jump = ticks_per_second > ANIM_GLIDE_MAX_SPEED || many_ticks;
    a->ticks_per_second = ticks_per_second;
    a->last_tick = s->tick;
    a->started = true;
    int n = s->pool.capacity < a->capacity ? s->pool.capacity : a->capacity;
    for (int i = 0; i < n; i++) {
        update_slot(&a->slots[i], &s->pool.items[i], s);
    }
}

/* A creature on the move walks, or runs when its move is quick. Its feet
 * take a step cycle of its own species, sped up when the glide outruns it, and
 * the frame follows the body clock, so the legs stop while paused. */
static void moving(const Anim *a, const AnimSlot *m, const Creature *c, const Sim *s,
                   uint64_t now_ms, AnimPose *out)
{
    out->action = m->move_ticks <= ANIM_RUN_TICKS ? ACTION_RUN : ACTION_WALK;
    SpriteBody b = sprite_body(c, &s->set, SPRITE_BASE_NORMAL);
    float cells_per_second = (float)a->ticks_per_second / m->move_ticks;
    float seconds = pose_cycle_seconds(&b, out->action) /
                    pose_cadence(&b, out->action, cells_per_second);
    out->frame = pose_frame(now_ms, c->id, out->action, seconds);
}

/* An action with a cycle of fixed length: its frame by the body clock. */
static void cycling(int action, const Creature *c, uint64_t now_ms, AnimPose *out)
{
    out->action = action;
    out->frame = pose_frame(now_ms, c->id, action, pose_cycle_seconds(NULL, action));
}

void anim_pose(const Anim *a, const Sim *s, int slot, float tick_fraction,
               uint64_t now_ms, AnimPose *out)
{
    const AnimSlot *m = &a->slots[slot];
    const Creature *c = &s->pool.items[slot];
    const World *w = &s->world;

    bool moved = m->from_x != m->x || m->from_y != m->y;
    float t = ((float)(s->tick - m->moved_tick) + tick_fraction) / m->move_ticks;
    out->x = (float)m->x;
    out->y = (float)m->y;
    if (moved && !a->jump && t < 1.0f) {
        float dx = (float)wrapped_step(m->from_x, m->x, w->w);
        float dy = (float)wrapped_step(m->from_y, m->y, w->h);
        out->x = (float)m->from_x + dx * t;
        out->y = (float)m->from_y + dy * t;
    }

    /* What its mouth is doing, for the marks and its facing. */
    out->bite = BITE_NOTHING;
    bool fruit = false;
    if (c->current.kind == ACT_ATTACK) {
        int target = creature_attack_target(w, &s->pool, slot, &s->set);
        if (target >= 0) {
            out->bite = POSE_ATTACK;
            out->target_x = s->pool.items[target].x;
            out->target_y = s->pool.items[target].y;
        }
    } else if (c->current.kind == ACT_BITE) {
        /* The simulation's own answer, so what is shown cannot drift from
         * the rules. It describes the next bite, so it can differ for a tick
         * from the one just taken, which nobody could see at this speed. */
        BiteChoice choice = creature_bite_choice(w, &s->pool, slot, &s->set);
        out->bite = choice.kind;
        fruit = choice.fruit;
        if (choice.kind != BITE_NOTHING) {
            out->target_x = choice.cell % w->w;
            out->target_y = choice.cell / w->w;
        }
    }

    /* Moving shows while the glide lasts, or in jump mode for the tick after
     * the move. */
    bool on_the_move = moved && (a->jump ? s->tick - m->moved_tick <= 1 : t < 1.0f);
    if (c->current.kind == ACT_SLEEP) {
        /* now_ms stops while paused, so a paused sleeper holds its breath. */
        cycling(ACTION_SLEEP, c, now_ms, out);
    } else if (on_the_move) {
        moving(a, m, c, s, now_ms, out);
    } else if (out->bite == BITE_PLANT && !fruit) {
        cycling(ACTION_GRAZE, c, now_ms, out);
    } else if (out->bite != BITE_NOTHING) {
        /* Fruit, carrion and, until the attack has its own cycle, a fight. */
        cycling(ACTION_BITE, c, now_ms, out);
    } else {
        cycling(ACTION_REST, c, now_ms, out); /* mating included */
    }
    /* Blinks are drawn over the picture: resting and moving. Asleep the eyes
     * are shut; biting, grazing and fighting they stay open. */
    bool blinks = out->action == ACTION_REST || out->action == ACTION_WALK ||
                  out->action == ACTION_RUN;
    out->eyes = out->action == ACTION_SLEEP ? 0.0f
              : blinks                      ? pose_eyes(now_ms, c->id)
                                            : 1.0f;
    out->flip = m->face < 0;
    /* A biter faces what it bites, whatever its heading says; straight above
     * or below keeps the facing it had. */
    if (out->bite != BITE_NOTHING) {
        int to_target = wrapped_step(c->x, out->target_x, w->w);
        if (to_target != 0) {
            out->flip = to_target < 0;
        }
    }
    float pop = ((float)(s->tick - m->born_tick) + tick_fraction) /
                (float)ANIM_POP_TICKS;
    out->pop = pop > 1.0f ? 1.0f : pop;
}
