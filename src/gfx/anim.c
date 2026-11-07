#include "anim.h"

#include <math.h>   /* fmodf, sinf */
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
    m->fight = FIGHT_NONE; /* a new creature in the slot has no fight yet */
    m->hit_by = -1;
    m->struck_ms = 0;
    m->puff_ms = 0;
}

static void update_slot(AnimSlot *m, const Creature *c, const Sim *s)
{
    if (!c->alive) {
        m->seen = false;
        m->fight = FIGHT_NONE;
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

/* ---- Fights ---- */

/* The slot the creature in `slot` attacks right now, by the simulation's own
 * answer, or -1 when it is not attacking anyone. */
static int attacked_now(const Sim *s, int slot)
{
    const Creature *c = &s->pool.items[slot];
    if (!c->alive || c->current.kind != ACT_ATTACK) {
        return -1;
    }
    return creature_attack_target(&s->world, &s->pool, slot, &s->set);
}

/* Whether the fight remembered in m is still the one the simulation shows:
 * it attacks the same slot, and the creature there is still the same one. */
static bool same_fight(const AnimSlot *m, const Sim *s, int target)
{
    return target >= 0 && target == m->victim_slot &&
           s->pool.items[target].id == m->victim_id;
}

static void start_fight(AnimSlot *m, const Sim *s, int target, bool jump,
                        uint64_t body_ms)
{
    m->fight = FIGHT_ON;
    /* No time for a stalk at high speed: straight to the pounce. */
    m->fight_c = jump ? POSE_POUNCE_AT : 0.0f;
    m->fight_ms = body_ms;
    m->victim_slot = target;
    m->victim_id = s->pool.items[target].id;
}

/* The simulation has stopped the fight: jump ahead to the bite, or past the
 * hold to the release, so it ends soon but never without its hit. The jump
 * itself is shown for a frame before the fight moves on. */
static void end_fight(AnimSlot *m)
{
    m->fight = FIGHT_ENDING;
    if (m->fight_c < POSE_IMPACT_AT) {
        m->fight_c = POSE_IMPACT_AT;
    } else if (m->fight_c >= POSE_HOLD_AT && m->fight_c < POSE_RELEASE_AT) {
        m->fight_c = POSE_RELEASE_AT;
    }
}

/* Moves a fight on by step, a share of the attack's length. */
static void advance_fight(AnimSlot *m, float step, bool jump)
{
    if (m->fight == FIGHT_ENDING) {
        bool impact = m->fight_c < POSE_HOLD_AT;
        m->fight_c += step * (impact ? ANIM_ENDING_IMPACT_RATE : ANIM_ENDING_RELEASE_RATE);
        if (m->fight_c >= POSE_HOLD_AT && m->fight_c < POSE_RELEASE_AT) {
            m->fight_c = POSE_RELEASE_AT; /* no holding on the way out */
        }
        if (m->fight_c >= 1.0f) {
            m->fight = FIGHT_NONE;
        }
        return;
    }
    m->fight_c += step;
    if (jump) {
        /* Only the pounce and the hit: past the impact straight to the
         * release, and after it a new pounce. */
        if (m->fight_c >= POSE_HOLD_AT && m->fight_c < POSE_RELEASE_AT) {
            m->fight_c = POSE_RELEASE_AT;
        } else if (m->fight_c >= 1.0f) {
            m->fight_c = POSE_POUNCE_AT;
        }
    } else if (m->fight_c >= POSE_RELEASE_AT) {
        /* A long fight holds: back to the hold's start, keeping what was over,
         * so the shake runs on at an even pace. */
        float hold = POSE_RELEASE_AT - POSE_HOLD_AT;
        m->fight_c = POSE_HOLD_AT + fmodf(m->fight_c - POSE_RELEASE_AT, hold);
    }
}

/* One slot's fight this frame: started, kept, ended or moved on. */
static void update_fight(AnimSlot *m, const Sim *s, int slot, bool jump, uint64_t body_ms)
{
    if (!m->seen) {
        return; /* dead or never seen: update_slot cleared it */
    }
    int target = attacked_now(s, slot);
    /* Body time only goes forward, but after a reset of the clock it could
     * look as if it went back: unsigned subtraction would wrap to a huge
     * number, hence the test first. */
    uint64_t elapsed = body_ms > m->fight_ms ? body_ms - m->fight_ms : 0;
    float step = (float)elapsed / (1000.0f * POSE_ATTACK_SECONDS);
    m->fight_ms = body_ms;
    float before = m->fight != FIGHT_NONE ? m->fight_c : 1.0f;
    switch (m->fight) {
    case FIGHT_NONE:
        if (target >= 0) {
            start_fight(m, s, target, jump, body_ms);
        }
        break;
    case FIGHT_ON:
        if (same_fight(m, s, target)) {
            advance_fight(m, step, jump);
        } else {
            end_fight(m); /* shown as it is for this frame */
        }
        break;
    default: /* FIGHT_ENDING plays out, whatever the simulation does now */
        advance_fight(m, step, jump);
        break;
    }
    /* Crossing the bite this frame, by playing on or by a jump to it. A
     * fight that has just started at 0.5 or past it has crossed nothing:
     * before stands at 1 then. */
    if (m->fight != FIGHT_NONE && before < POSE_IMPACT_AT && m->fight_c >= POSE_IMPACT_AT) {
        m->struck_ms = body_ms;
    }
}

/* Tells every victim who hits it: each slot whose fight is on or ending marks
 * the slot it attacks, if the creature there is still the one it attacked.
 * Slots are gone through in order and the first mark stays, so when two
 * attack one, the lower slot wins. */
static void mark_victims(Anim *a, const Sim *s, int n)
{
    for (int i = 0; i < n; i++) {
        a->slots[i].hit_by = -1;
    }
    for (int i = 0; i < n; i++) {
        const AnimSlot *m = &a->slots[i];
        int v = m->victim_slot;
        if (m->fight == FIGHT_NONE || v < 0 || v >= n) {
            continue;
        }
        const Creature *victim = &s->pool.items[v];
        if (victim->alive && victim->id == m->victim_id && a->slots[v].hit_by < 0) {
            a->slots[v].hit_by = i;
        }
    }
}

void anim_update(Anim *a, const Sim *s, int ticks_per_second, uint64_t body_ms)
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
    /* After every slot is up to date, so a fight sees its victim's new id. */
    bool fast = ticks_per_second > ANIM_GLIDE_MAX_SPEED;
    for (int i = 0; i < n; i++) {
        update_fight(&a->slots[i], s, i, fast, body_ms);
    }
    mark_victims(a, s, n);
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

/* The frame of the attack at share c: 54 frames over the whole of it. */
static int attack_frame(float c)
{
    int n = SPRITE_FRAMES[ACTION_ATTACK];
    int f = (int)(c * (float)n); /* (int) cuts towards zero: the frame c is in */
    return f < 0 ? 0 : (f >= n ? n - 1 : f);
}

/* Art pixels in a cell at Normal: knock-back and trembling are in them. */
#define ART_PIXELS_PER_CELL 16.0f

/* The creature in `slot` being hit by the fight of slot m->hit_by: its flash,
 * and how far it is pushed away from the attacker, in cells. Nothing when it
 * is not being hit, or the fight is for an earlier creature in its slot. */
static void being_hit(const Anim *a, const Sim *s, int slot, uint64_t now_ms,
                      AnimPose *out)
{
    out->flash = out->knock_x = out->tremble = 0.0f;
    int by = a->slots[slot].hit_by;
    if (by < 0 || by >= a->capacity) {
        return;
    }
    const AnimSlot *attacker = &a->slots[by];
    const Creature *victim = &s->pool.items[slot];
    if (attacker->fight == FIGHT_NONE || attacker->victim_slot != slot ||
        attacker->victim_id != victim->id) {
        return;
    }
    float knock, tremble;
    pose_hit(attacker->fight_c, &out->flash, &knock, &tremble);
    /* Away from the attacker: east when it stands to the west. Straight above
     * or below, away from where it faces. */
    const Creature *hitter = &s->pool.items[by];
    int dx = wrapped_step(hitter->x, victim->x, s->world.w);
    float away = dx > 0 ? 1.0f : (dx < 0 ? -1.0f : (float)attacker->face);
    /* The push is in K units of the attacker's body, as the mockup measures
     * it, at Normal, where a cell is 16 art pixels. */
    SpriteBody b = sprite_body(hitter, &s->set, SPRITE_BASE_NORMAL);
    out->knock_x = away * knock * sprite_k(&b) / ART_PIXELS_PER_CELL;
    /* The tremble (v9:682): sin(t * 40), t the body clock in seconds. */
    float t = (float)(now_ms % 60000u) / 1000.0f;
    out->tremble = tremble * sinf(t * 40.0f) / ART_PIXELS_PER_CELL;
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
    bool fighting = m->fight != FIGHT_NONE;
    if (fighting) {
        /* The victim it remembers, if that creature is still there; an ending
         * fight whose victim is gone keeps the facing it had. */
        const Creature *v = &s->pool.items[m->victim_slot];
        if (v->alive && v->id == m->victim_id) {
            out->bite = POSE_ATTACK;
            out->target_x = v->x;
            out->target_y = v->y;
        }
    } else if (c->current.kind == ACT_ATTACK) {
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
    } else if (fighting) {
        out->action = ACTION_ATTACK;
        out->frame = attack_frame(m->fight_c);
    } else if (on_the_move) {
        moving(a, m, c, s, now_ms, out);
    } else if (out->bite == BITE_PLANT && !fruit) {
        cycling(ACTION_GRAZE, c, now_ms, out);
    } else if (out->bite != BITE_NOTHING) {
        /* Fruit or carrion; or a fight the next anim_update will start. */
        cycling(ACTION_BITE, c, now_ms, out);
    } else {
        cycling(ACTION_REST, c, now_ms, out); /* mating included */
    }
    /* Blinks are drawn over the picture: resting and moving. Asleep the eyes
     * are shut; biting, grazing and attacking they stay open. */
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
    being_hit(a, s, slot, now_ms, out);
}
