#include "creature.h"

#include <stdlib.h> /* malloc, free */

/* One step in each heading, in HEADING_ order: north is up, which is
 * row y - 1. */
static const int STEP_X[HEADING_COUNT] = {0, 1, 0, -1};
static const int STEP_Y[HEADING_COUNT] = {-1, 0, 1, 0};

/* The eight neighbours of a cell, clockwise from north. */
static const int AROUND_X[8] = {0, 1, 1, 1, 0, -1, -1, -1};
static const int AROUND_Y[8] = {-1, -1, 0, 1, 1, 1, 0, -1};

bool pool_init(Pool *p, int capacity)
{
    *p = (Pool){0};
    if (capacity < 1) {
        return false;
    }
    /* sizeof(*p->items) is the size of one Creature, worked out by the
     * compiler from the pointer's type; it stays right if the type of
     * items ever changes. */
    p->items = malloc((size_t)capacity * sizeof(*p->items));
    if (p->items == NULL) {
        return false;
    }
    p->capacity = capacity;
    pool_clear(p);
    return true;
}

void pool_clear(Pool *p)
{
    for (int i = 0; i < p->capacity; i++) {
        p->items[i] = (Creature){0};
        /* Each free slot points at the next; the last ends the list. */
        p->items[i].next_free = i + 1 < p->capacity ? i + 1 : -1;
    }
    p->free_head = 0;
    p->count = 0;
    p->next_id = 0;
}

void pool_free(Pool *p)
{
    free(p->items);
    *p = (Pool){0};
}

int pool_spawn(Pool *p, World *w, int x, int y)
{
    int cell = world_idx(w, x, y);
    if (p->free_head < 0 || w->terrain[cell] != TERRAIN_SOIL ||
        w->occupant[cell] >= 0) {
        return -1;
    }
    /* Take the first free slot off the list. */
    int idx = p->free_head;
    Creature *c = &p->items[idx];
    p->free_head = c->next_free;

    /* A compound literal assigned to *c: every field not named is 0
     * (false, 0.0f, ACT_REST). The coordinates are stored wrapped, the
     * same way world_idx sees them. */
    *c = (Creature){
        .alive = true,
        .next_free = -1,
        .id = p->next_id,
        .lineage = p->next_id,
        .x = cell % w->w,
        .y = cell / w->w,
        .heading = HEADING_NORTH,
    };
    /* An unsigned counter: after 2^32 - 1 it wraps to 0, which is defined
     * in C (unlike signed overflow). At 1,000 births a second that takes
     * over 49 days. */
    p->next_id++;
    p->count++;
    w->occupant[cell] = idx;
    return idx;
}

void pool_kill(Pool *p, World *w, int idx, const Settings *s)
{
    (void)s; /* lesson 7: the body becomes carrion, which needs settings */
    Creature *c = &p->items[idx];
    if (!c->alive) {
        return;
    }
    w->occupant[world_idx(w, c->x, c->y)] = -1;
    c->alive = false;
    /* Put the slot at the front of the free list: the next spawn reuses
     * it. Its old contents stay until then, but alive is false. */
    c->next_free = p->free_head;
    p->free_head = idx;
    p->count--;
}

/* The heading after turning: turns is 1 for right, 3 for left. heading
 * is promoted to int for the +, and the result, 0..3, fits back into a
 * uint8_t, which the cast says out loud. */
static uint8_t turned(uint8_t heading, int turns)
{
    return (uint8_t)((heading + turns) % HEADING_COUNT);
}

/* The cell one step from (x, y) in the given heading. */
static int cell_towards(const World *w, int x, int y, uint8_t heading)
{
    return world_idx(w, x + STEP_X[heading], y + STEP_Y[heading]);
}

static float clamp01(float v)
{
    if (v < 0.0f) {
        return 0.0f;
    }
    return v > 1.0f ? 1.0f : v;
}

void creature_sense(const World *w, const Pool *p, int idx,
                    const Settings *s, Senses *out)
{
    const Creature *c = &p->items[idx];
    /* Every sense starts at 0, including those not filled in yet. */
    *out = (Senses){0};
    out->v[SENSE_ENERGY] = clamp01(c->energy / s->split_threshold);
    /* age / (age + half) rises from 0 towards 1 and is 0.5 at half: no
     * upper limit on age is needed. */
    float age = (float)c->age;
    out->v[SENSE_AGE] = age / (age + s->sense_age_half);

    /* The directions of the three rays, in RAY_ order. */
    const uint8_t ray_heading[RAY_COUNT] = {
        c->heading, turned(c->heading, 3), turned(c->heading, 1)};
    for (int ray = 0; ray < RAY_COUNT; ray++) {
        int cell = cell_towards(w, c->x, c->y, ray_heading[ray]);
        float veg = (float)w->veg[cell] / (float)VEG_MAX;
        out->v[SENSE_RAYS + ray * RAY_VALUES + RAY_VEG] = veg;
    }
}

/* Moves one cell forward if that cell is free soil. The move is paid
 * for either way: trying costs as much as succeeding. */
static void act_move(World *w, Creature *c, int idx, const Settings *s)
{
    c->energy -= s->move_cost;
    int from = world_idx(w, c->x, c->y);
    int to = cell_towards(w, c->x, c->y, c->heading);
    if (w->terrain[to] != TERRAIN_SOIL || w->occupant[to] >= 0) {
        return;
    }
    w->occupant[from] = -1;
    w->occupant[to] = idx;
    c->x = to % w->w;
    c->y = to / w->w;
}

/* Eats one step of density from the vegetation ahead. When that leaves
 * the cell bare, flora_step resets the cell's age on the next tick. */
static void act_bite(World *w, Creature *c, const Settings *s)
{
    int ahead = cell_towards(w, c->x, c->y, c->heading);
    if (w->veg[ahead] == 0) {
        return;
    }
    w->veg[ahead]--;
    c->energy += s->bite_veg_energy;
}

/* Splits into two when energy allows and a neighbouring cell is free.
 * The search starts at a random neighbour, so children do not always
 * appear on the same side. */
static int act_split(World *w, Pool *p, int idx, const Settings *s, Rng *r)
{
    Creature *parent = &p->items[idx];
    if (parent->energy < s->split_threshold) {
        return -1;
    }
    int start = (int)rng_below(r, 8);
    for (int k = 0; k < 8; k++) {
        int dir = (start + k) % 8;
        int child_idx = pool_spawn(p, w, parent->x + AROUND_X[dir],
                                   parent->y + AROUND_Y[dir]);
        if (child_idx < 0) {
            continue; /* not free soil, or the pool is full */
        }
        /* parent is still valid here: spawning never moves the array,
         * which was allocated once. Had items been reallocated to grow,
         * parent would now point at freed memory. */
        Creature *child = &p->items[child_idx];
        float share = parent->energy * s->split_share;
        child->energy = share;
        parent->energy -= share;
        child->lineage = parent->lineage;
        child->heading = (uint8_t)rng_below(r, HEADING_COUNT);
        return child_idx;
    }
    return -1;
}

int creature_act(World *w, Pool *p, int idx, Action a, const Settings *s,
                 Rng *r)
{
    Creature *c = &p->items[idx];
    switch (a.kind) {
    case ACT_TURN_LEFT:
        c->heading = turned(c->heading, 3);
        break;
    case ACT_TURN_RIGHT:
        c->heading = turned(c->heading, 1);
        break;
    case ACT_MOVE:
        act_move(w, c, idx, s);
        break;
    case ACT_BITE:
        act_bite(w, c, s);
        break;
    case ACT_SPLIT:
        return act_split(w, p, idx, s, r);
    default:
        break; /* ACT_REST, or a kind no brain should send */
    }
    return -1;
}

bool creature_upkeep(Creature *c, const Settings *s)
{
    c->energy -= s->upkeep_base;
    if (c->age < UINT32_MAX) {
        c->age++;
    }
    return c->energy > 0.0f;
}
