#include "creature.h"

#include <math.h>   /* powf */
#include <stdlib.h> /* malloc, free */

#include "flora.h" /* carrion_add, CARRION_MIN */

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
        .level = LEVEL_START,
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
    Creature *c = &p->items[idx];
    if (!c->alive) {
        return;
    }
    int cell = world_idx(w, c->x, c->y);
    /* The body, plus whatever energy was left: a starved or killed
     * creature has none (its energy is 0 or below), one dying of old age
     * may have plenty. */
    float left = c->energy > 0.0f ? c->energy : 0.0f;
    carrion_add(w, cell, s->body_energy * creature_size(c, s) + left,
                c->level);
    w->occupant[cell] = -1;
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

float creature_size(const Creature *c, const Settings *s)
{
    float gene = c->genome.g[GENE_SIZE];
    return s->size_min + gene * (s->size_max - s->size_min);
}

float creature_speed(const Creature *c, const Settings *s)
{
    float gene = c->genome.g[GENE_SPEED];
    return s->speed_min + gene * (1.0f - s->speed_min);
}

int creature_sight(const Creature *c, const Settings *s)
{
    float gene = c->genome.g[GENE_SIGHT];
    /* + 0.5f then converting to int rounds to the nearest whole cell. */
    return 1 + (int)(gene * (float)(s->sight_max - 1) + 0.5f);
}

float creature_upkeep_cost(const Creature *c, const Settings *s)
{
    float digest = c->genome.g[GENE_PLANT] + c->genome.g[GENE_MEAT];
    return s->upkeep_base + s->size_upkeep * creature_size(c, s) +
           s->speed_upkeep * creature_speed(c, s) +
           s->sight_upkeep * (float)creature_sight(c, s) +
           s->digest_upkeep * digest * digest;
}

/* The energy at which this creature may split: bigger bodies need more. */
static float split_needs(const Creature *c, const Settings *s)
{
    return s->split_threshold * creature_size(c, s);
}

float attack_chance(float a, float t, float k)
{
    float ak = powf(a, k);
    float tk = powf(t, k);
    /* Both are positive for positive sizes; the guard keeps a zero size
     * from dividing 0 by 0, which would give NaN. */
    if (ak + tk <= 0.0f) {
        return 0.5f;
    }
    return ak / (ak + tk);
}

void creature_ate(Creature *c, float energy, float source_level)
{
    /* The division below is by eaten + energy. eaten is never negative,
     * so it is 0 only when nothing has been eaten and energy is 0: the
     * check here rules that out. */
    if (energy <= 0.0f) {
        return;
    }
    float total = c->eaten + energy;
    c->level = 1.0f + (c->eaten * (c->level - 1.0f) + energy * source_level) /
                          total;
    c->eaten = total;
}

/* What one ray sees, written into its RAY_VALUES slots of out. The first
 * cell gives the vegetation. Then the ray walks up to sight cells,
 * stopping at rock, and reports the nearest creature (its closeness,
 * relative size and kinship) and, separately, the closeness of the
 * nearest carrion: a creature in the way hides no carrion behind it. */
static void sense_ray(const World *w, const Pool *p, const Creature *c,
                      uint8_t heading, const Settings *s, float *out)
{
    int first = cell_towards(w, c->x, c->y, heading);
    out[RAY_VEG] = (float)w->veg[first] / (float)VEG_MAX;

    int sight = creature_sight(c, s);
    bool creature_found = false, carrion_found = false;
    for (int d = 1; d <= sight; d++) {
        int cell = world_idx(w, c->x + d * STEP_X[heading],
                             c->y + d * STEP_Y[heading]);
        if (w->terrain[cell] == TERRAIN_ROCK) {
            return; /* rock blocks the view; water does not */
        }
        float closeness = 1.0f - (float)(d - 1) / (float)sight;
        if (!carrion_found && w->carrion[cell] > 0.0f) {
            out[RAY_CARRION] = closeness;
            carrion_found = true;
        }
        if (!creature_found && w->occupant[cell] >= 0) {
            const Creature *seen = &p->items[w->occupant[cell]];
            float a = creature_size(c, s);
            float t = creature_size(seen, s);
            out[RAY_NEAR] = closeness;
            out[RAY_RELSIZE] = t / (a + t);
            out[RAY_KIN] = genome_kinship(&c->genome, &seen->genome);
            creature_found = true;
        }
        if (creature_found && carrion_found) {
            return;
        }
    }
}

void creature_sense(const World *w, const Pool *p, int idx,
                    const Settings *s, Senses *out)
{
    const Creature *c = &p->items[idx];
    /* Every sense starts at 0, including those not filled in yet. */
    *out = (Senses){0};
    out->v[SENSE_ENERGY] = clamp01(c->energy / split_needs(c, s));
    /* age / (age + half) rises from 0 towards 1 and is 0.5 at half: no
     * upper limit on age is needed. */
    float age = (float)c->age;
    out->v[SENSE_AGE] = age / (age + s->sense_age_half);
    out->v[SENSE_SIZE] = c->genome.g[GENE_SIZE];
    out->v[SENSE_DIET] = c->genome.g[GENE_MEAT]; /* how much a hunter */

    /* The directions of the three rays, in RAY_ order. */
    const uint8_t ray_heading[RAY_COUNT] = {
        c->heading, turned(c->heading, 3), turned(c->heading, 1)};
    for (int ray = 0; ray < RAY_COUNT; ray++) {
        /* A pointer into the middle of the array: the ray's first slot.
         * sense_ray writes out[0] .. out[RAY_VALUES - 1] from there. */
        float *slots = &out->v[SENSE_RAYS + ray * RAY_VALUES];
        sense_ray(w, p, c, ray_heading[ray], s, slots);
    }
}

/* Moves one cell forward if that cell is free soil and the creature has
 * a move point. With a point, the move is paid for either way: trying
 * costs as much as succeeding. Without one, nothing happens. */
static void act_move(World *w, Creature *c, int idx, const Settings *s)
{
    if (c->move_points < 1.0f - MOVE_SLACK) {
        return;
    }
    c->move_points -= 1.0f;
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

/* An attack on the creature ahead. The target is only hurt: if its
 * energy reaches 0 it stops acting, and the end-of-tick accounting kills
 * it and leaves its body as carrion. */
static void attack(Creature *c, Creature *target, const Settings *s, Rng *r)
{
    c->energy -= s->attack_cost;
    float a = creature_size(c, s);
    float chance = attack_chance(a, creature_size(target, s),
                                 s->attack_steepness);
    if (rng_unit(r) < chance) {
        target->energy -= s->bite_damage * a;
    }
}

/* How much one bite of carrion on cell i would take, and give this
 * creature. */
static float carrion_take(const World *w, int i, const Settings *s)
{
    return w->carrion[i] < s->bite_meat_energy ? w->carrion[i]
                                                 : s->bite_meat_energy;
}

/* The share of a food's energy a digestion gene of this value gets out
 * of it: from digest_floor (gene 0) to 1 (gene 1). */
static float digest_eff(float gene, const Settings *s)
{
    return s->digest_floor + (1.0f - s->digest_floor) * gene;
}

static float meat_gain(const Creature *c, float take, const Settings *s)
{
    return take * digest_eff(c->genome.g[GENE_MEAT], s);
}

static float plant_gain(const Creature *c, const Settings *s)
{
    return s->bite_veg_energy * digest_eff(c->genome.g[GENE_PLANT], s);
}

/* Eats up to bite_meat_energy of the carrion ahead. */
static void eat_carrion(World *w, Creature *c, int ahead, const Settings *s)
{
    float take = carrion_take(w, ahead, s);
    float level = w->carrion_level[ahead]; /* read before it may be reset */
    w->carrion[ahead] -= take;
    if (w->carrion[ahead] < CARRION_MIN) {
        w->carrion[ahead] = 0.0f;
        w->carrion_level[ahead] = 0.0f;
    }
    float gain = meat_gain(c, take, s);
    c->energy += gain;
    creature_ate(c, gain, level);
}

/* Eats one step of density from the vegetation ahead. When that leaves
 * the cell bare, flora_step resets the cell's age on the next tick. */
static void eat_plant(World *w, Creature *c, int ahead, const Settings *s)
{
    w->veg[ahead]--;
    float gain = plant_gain(c, s);
    c->energy += gain;
    creature_ate(c, gain, 1.0f); /* plants are level 1 */
}

/* A bite at whatever is ahead: a creature is attacked. Otherwise the
 * bite eats carrion or vegetation; on a cell with both, whichever gives
 * this creature more energy (carrion on a tie). Biting an empty cell does
 * nothing. */
static void act_bite(World *w, Pool *p, Creature *c, const Settings *s,
                     Rng *r)
{
    int ahead = cell_towards(w, c->x, c->y, c->heading);
    if (w->occupant[ahead] >= 0) {
        attack(c, &p->items[w->occupant[ahead]], s, r);
        return;
    }
    bool has_carrion = w->carrion[ahead] > 0.0f;
    bool has_plant = w->veg[ahead] > 0;
    if (has_carrion && has_plant) {
        if (plant_gain(c, s) > meat_gain(c, carrion_take(w, ahead, s), s)) {
            eat_plant(w, c, ahead, s);
        } else {
            eat_carrion(w, c, ahead, s);
        }
    } else if (has_carrion) {
        eat_carrion(w, c, ahead, s);
    } else if (has_plant) {
        eat_plant(w, c, ahead, s);
    }
}

/* Splits into two when energy allows and a neighbouring cell is free.
 * The search starts at a random neighbour, so children do not always
 * appear on the same side. */
static int act_split(World *w, Pool *p, int idx, const Settings *s, Rng *r)
{
    Creature *parent = &p->items[idx];
    if (parent->energy < split_needs(parent, s)) {
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
        child->level = parent->level; /* until its own first meal */
        child->heading = (uint8_t)rng_below(r, HEADING_COUNT);
        /* Pointers to the two genomes, not copies: the function writes
         * the child's in place, reading the parent's. */
        genome_copy_mutated(&child->genome, &parent->genome, r, s);
        return child_idx;
    }
    return -1;
}

int creature_act(World *w, Pool *p, int idx, Action a, const Settings *s,
                 Rng *r)
{
    Creature *c = &p->items[idx];
    /* Move points build up every tick, whatever the action, but never
     * past 1: a creature cannot save up for a burst of moves. */
    c->move_points += creature_speed(c, s);
    if (c->move_points > 1.0f) {
        c->move_points = 1.0f;
    }
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
        act_bite(w, p, c, s, r);
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
    c->energy -= creature_upkeep_cost(c, s);
    if (c->age < UINT32_MAX) {
        c->age++;
    }
    /* max_age is an int and age a uint32_t: comparing them directly would
     * convert the int to unsigned (a negative one to a huge number), which
     * -Wextra warns about. Checking > 0 first makes the cast safe. */
    if (s->max_age > 0 && c->age >= (uint32_t)s->max_age) {
        return false;
    }
    return c->energy > 0.0f;
}
