#include "anim.h"

#include <stdlib.h> /* calloc, free */
#include <string.h> /* memset */

#include "sprite.h" /* the FRAME_ values */

/* How long one breath of a resting creature lasts, in milliseconds. */
#define BREATH_MS 625u
/* How long each half of a chomp lasts, in milliseconds. */
#define CHOMP_MS 150u

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
    a->last_tick = s->tick;
    a->started = true;
    int n = s->pool.capacity < a->capacity ? s->pool.capacity : a->capacity;
    for (int i = 0; i < n; i++) {
        update_slot(&a->slots[i], &s->pool.items[i], s);
    }
}

/* The frame for a creature not doing anything with a frame of its own. The
 * id offsets the breath, so a crowd does not pulse together. */
static int idle_or_walk_frame(const Anim *a, const AnimSlot *m, const Sim *s,
                              float t, bool moved, uint64_t now_ms)
{
    if (moved && a->jump && s->tick - m->moved_tick <= 1) {
        return FRAME_WALK0 + (int)(s->tick % 4u);
    }
    if (moved && !a->jump && t < 1.0f) {
        return FRAME_WALK0 + (int)(t * 4.0f);
    }
    return FRAME_REST0 + (int)((now_ms / BREATH_MS + m->id) % 2u);
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

    out->bite = BITE_NOTHING;
    if (c->current.kind == ACT_BITE) {
        /* The simulation's own answer, so what is shown cannot drift from
         * the rules. It describes the next bite, so it can differ for a tick
         * from the one just taken, which nobody could see at this speed. */
        BiteChoice choice = creature_bite_choice(w, &s->pool, slot, &s->set);
        out->bite = choice.kind;
        if (choice.kind != BITE_NOTHING) {
            out->target_x = choice.cell % w->w;
            out->target_y = choice.cell / w->w;
        }
    }

    if (out->bite != BITE_NOTHING) {
        /* Mouth open, mouth shut. The id keeps a crowd out of step. */
        bool open = (now_ms / CHOMP_MS + c->id) % 2u == 0;
        out->frame = open ? FRAME_BITE : FRAME_REST0;
    } else if (c->current.kind == ACT_MATE) {
        out->frame = FRAME_MATE;
    } else {
        out->frame = idle_or_walk_frame(a, m, s, t, moved, now_ms);
    }
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
