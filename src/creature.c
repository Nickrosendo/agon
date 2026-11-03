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
    p->plant_eaters = 0;
    p->births_refused = 0;
}

void pool_count_plant_eaters(Pool *p, const Settings *s)
{
    int n = 0;
    for (int i = 0; i < p->capacity; i++) {
        if (p->items[i].alive &&
            creature_counts_as_plant_eater(&p->items[i], s)) {
            n++;
        }
    }
    p->plant_eaters = n;
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

/* What age costs this creature per tick. e is how far it has aged: its age
 * scaled by how fast its genes age it. Squared, so that the cost is small for
 * most of a life and then climbs until food cannot pay for it: a practical end
 * set by genes and food, not a tick that kills a healthy creature. Slow ageing
 * is paid for from birth. Floats, so a huge age gives a huge cost and not a
 * wrapped one. */
static float ageing_cost(const Creature *c, const Settings *s)
{
    float gene = c->genome.g[GENE_AGEING];
    float rate = s->ageing_min_rate + (1.0f - s->ageing_min_rate) * gene;
    float e = (float)c->age * rate / s->ageing_span;
    return s->ageing_upkeep * e * e + s->slow_ageing_upkeep * (1.0f - gene);
}

float creature_upkeep_cost(const Creature *c, const Settings *s)
{
    float digest = c->genome.g[GENE_PLANT] + c->genome.g[GENE_MEAT];
    return s->upkeep_base + s->size_upkeep * creature_size(c, s) +
           s->speed_upkeep * creature_speed(c, s) +
           s->sight_upkeep * (float)creature_sight(c, s) +
           s->digest_upkeep * digest * digest + ageing_cost(c, s);
}

/* The energy at which this creature may mate: bigger bodies need more. */
static float mate_needs(const Creature *c, const Settings *s)
{
    return s->mate_threshold * creature_size(c, s);
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

/* What one ray senses, written into its RAY_VALUES slots of out. The
 * first cell gives the vegetation and the scent of carrion. Then the ray
 * walks up to sight cells, stopping at rock, and reports the nearest plant's
 * closeness and the nearest creature: its closeness, relative size, kinship,
 * whether it hunts, whether it wishes to mate and how alike it looks. A creature
 * blocks the view of whatever is behind it. */
static void sense_ray(const World *w, const Pool *p, const Creature *c,
                      uint8_t heading, const Settings *s, float *out)
{
    int first = cell_towards(w, c->x, c->y, heading);
    out[RAY_VEG] = (float)w->veg[first] / (float)VEG_MAX;
    /* The scent on that first cell, squashed into 0..1: half at one
     * bite's worth. The three rays together say which way it grows. */
    float scent = w->scent[first];
    out[RAY_CARRION] = scent / (scent + s->bite_meat_energy);

    int sight = creature_sight(c, s);
    for (int d = 1; d <= sight; d++) {
        int cell = world_idx(w, c->x + d * STEP_X[heading],
                             c->y + d * STEP_Y[heading]);
        if (w->terrain[cell] == TERRAIN_ROCK) {
            return; /* rock blocks the view; water does not */
        }
        /* Still 0 means none seen yet, and a plant in sight always reads above
         * 0: d - 1 is below sight. */
        if (out[RAY_PLANT] == 0.0f && w->veg[cell] > 0) {
            out[RAY_PLANT] = 1.0f - (float)(d - 1) / (float)sight;
        }
        if (w->occupant[cell] >= 0) {
            const Creature *seen = &p->items[w->occupant[cell]];
            float a = creature_size(c, s);
            float t = creature_size(seen, s);
            out[RAY_NEAR] = 1.0f - (float)(d - 1) / (float)sight;
            out[RAY_RELSIZE] = t / (a + t);
            out[RAY_KIN] = genome_kinship(&c->genome, &seen->genome);
            /* A bool converted to float is exactly 0.0f or 1.0f. */
            out[RAY_HUNTER] = (float)creature_is_hunter(seen);
            out[RAY_READY] = seen->mate_wish > 0 ? 1.0f : 0.0f;
            out[RAY_ALIKE] = genome_body_likeness(&c->genome, &seen->genome);
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
    out->v[SENSE_ENERGY] = clamp01(c->energy / mate_needs(c, s));
    /* age / (age + half) rises from 0 towards 1 and is 0.5 at half: no
     * upper limit on age is needed. */
    float age = (float)c->age;
    out->v[SENSE_AGE] = age / (age + s->sense_age_half);
    out->v[SENSE_SIZE] = c->genome.g[GENE_SIZE];
    out->v[SENSE_DIET] = c->genome.g[GENE_MEAT]; /* how much a hunter */
    out->v[SENSE_SEASON] = p->in_season ? 1.0f : 0.0f;
    /* The same test act_move makes, so the sense cannot disagree with walking. */
    int ahead = cell_towards(w, c->x, c->y, c->heading);
    out->v[SENSE_BLOCKED] =
        w->terrain[ahead] != TERRAIN_SOIL || w->occupant[ahead] >= 0 ? 1.0f : 0.0f;

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

/* An attack by c on target. The target is only hurt: if its
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

/* What a bite of a plant with the given rule gives c: less for a
 * vigorous rule (veg_vigour_food), less for a weak plant gene. */
static float plant_gain(const Creature *c, uint8_t rule, const Settings *s)
{
    float food = 1.0f - s->veg_vigour_food * veg_rule_vigour(rule);
    return s->bite_veg_energy * food * digest_eff(c->genome.g[GENE_PLANT], s);
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
    float gain = plant_gain(c, w->veg_rule[ahead], s);
    c->energy += gain;
    creature_ate(c, gain, 1.0f); /* plants are level 1 */
}

bool creature_is_hunter(const Creature *c)
{
    return c->genome.g[GENE_MEAT] > c->genome.g[GENE_PLANT];
}

bool creature_counts_as_plant_eater(const Creature *c, const Settings *s)
{
    if (s->cap_by_level) {
        return c->level < LEVEL_MEAT;
    }
    return !creature_is_hunter(c);
}

/* The reasoning behind creature_bite_choice:
 *
 * A bite looks at the eight cells around the creature, starting with the
 * one ahead and going clockwise; on a tie, the first one seen wins.
 *
 * Food on a cell holding a creature is covered by it and is not eaten.
 * Among the free cells, the bite takes the food (plants or carrion) that
 * gives this creature the most energy, carrion first on a tie; a bite
 * that would gain nothing does nothing.
 *
 * A hunter (meat gene above plant gene) with no carrion on any of the
 * eight cells attacks instead: the neighbouring plant-eater it is most
 * likely to hit. Plant-eaters never attack. */
BiteChoice creature_bite_choice(const World *w, const Pool *p, int idx,
                                const Settings *s)
{
    const Creature *c = &p->items[idx];
    bool hunter = creature_is_hunter(c);
    float my_size = creature_size(c, s);
    int food_cell = -1;
    bool food_is_carrion = false;
    float best_gain = 0.0f;
    bool carrion_near = false;
    int prey = -1;
    int prey_cell = -1;
    float best_chance = 0.0f;

    /* AROUND_ is clockwise from north, two steps per heading. */
    int start = 2 * c->heading;
    for (int k = 0; k < 8; k++) {
        int dir = (start + k) % 8;
        int cell = world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir]);
        int other = w->occupant[cell];
        if (other >= 0) {
            const Creature *o = &p->items[other];
            if (hunter && !creature_is_hunter(o) && o->energy > 0.0f) {
                float chance = attack_chance(my_size, creature_size(o, s),
                                             s->attack_steepness);
                if (chance > best_chance) {
                    best_chance = chance;
                    prey = other;
                    prey_cell = cell;
                }
            }
            continue; /* the food under a creature is covered */
        }
        if (w->carrion[cell] > 0.0f) {
            carrion_near = true;
            float gain = meat_gain(c, carrion_take(w, cell, s), s);
            if (gain > best_gain) {
                best_gain = gain;
                food_cell = cell;
                food_is_carrion = true;
            }
        }
        if (w->veg[cell] > 0) {
            float gain = plant_gain(c, w->veg_rule[cell], s);
            if (gain > best_gain) {
                best_gain = gain;
                food_cell = cell;
                food_is_carrion = false;
            }
        }
    }

    BiteChoice choice = {BITE_NOTHING, -1, -1};
    if (hunter && !carrion_near && prey >= 0) {
        choice.kind = BITE_ATTACK;
        choice.cell = prey_cell;
        choice.prey = prey;
    } else if (food_cell >= 0) {
        choice.kind = food_is_carrion ? BITE_CARRION : BITE_PLANT;
        choice.cell = food_cell;
    }
    return choice;
}

static void act_bite(World *w, Pool *p, int idx, const Settings *s, Rng *r)
{
    Creature *c = &p->items[idx];
    BiteChoice choice = creature_bite_choice(w, p, idx, s);
    switch (choice.kind) {
    case BITE_ATTACK:
        attack(c, &p->items[choice.prey], s, r);
        break;
    case BITE_CARRION:
        eat_carrion(w, c, choice.cell, s);
        break;
    case BITE_PLANT:
        eat_plant(w, c, choice.cell, s);
        break;
    default:
        /* Nothing in reach: the effort is wasted, and it is paid for. */
        c->energy -= s->bite_empty_cost;
        break;
    }
}

/* Grown up and fed enough to have a child. mate_age is an int checked to be
 * 0 or more when settings load, so the cast cannot make a huge number of it.
 * The check for energy above 0 is for a mate_threshold of 0: a creature an
 * attack has just taken to 0 is dying, and must not become a parent. */
static bool can_mate(const Creature *c, const Settings *s)
{
    return c->age >= (uint32_t)s->mate_age && c->energy > 0.0f &&
           c->energy >= mate_needs(c, s);
}

/* The slot of a neighbour that wishes to mate and still can, looking round
 * from a random start so that no side is favoured; -1 for none. */
static int find_partner(const World *w, const Pool *p, const Creature *c,
                        const Settings *s, Rng *r)
{
    int start = (int)rng_below(r, 8);
    for (int k = 0; k < 8; k++) {
        int dir = (start + k) % 8;
        int cell = world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir]);
        int other = w->occupant[cell];
        if (other >= 0 && p->items[other].mate_wish > 0 &&
            can_mate(&p->items[other], s)) {
            return other;
        }
    }
    return -1;
}

/* A free soil cell beside c, looking round from start; -1 for none. */
static int free_cell_around(const World *w, const Creature *c, int start)
{
    for (int k = 0; k < 8; k++) {
        int dir = (start + k) % 8;
        int cell = world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir]);
        if (w->terrain[cell] == TERRAIN_SOIL && w->occupant[cell] < 0) {
            return cell;
        }
    }
    return -1;
}

/* Fills in a newborn from its parents. Both pay here, together, so the energy
 * is only moved: the three of them hold afterwards what the two held before.
 * a and b stay valid across the pool_spawn before this: spawning never moves
 * the array, which was allocated once. Had items been reallocated to grow,
 * they would now point at freed memory. */
static void make_child(Creature *child, Creature *a, Creature *b,
                       const Settings *s, Rng *r)
{
    float from_a = a->energy * s->mate_share;
    float from_b = b->energy * s->mate_share;
    a->energy -= from_a;
    b->energy -= from_b;
    child->energy = from_a + from_b;
    /* Each is a separate statement, so the draws have a fixed order. */
    child->lineage = rng_below(r, 2) == 0 ? a->lineage : b->lineage;
    child->heading = (uint8_t)rng_below(r, HEADING_COUNT);
    child->level = (a->level + b->level) / 2.0f; /* until its first meal */
    /* Pointers to the three genomes, not copies: the function writes the
     * child's in place, reading its parents'. */
    genome_cross(&child->genome, &a->genome, &b->genome, r, s);
    a->mate_wish = 0;
    b->mate_wish = 0;
}

/* See creature_act in creature.h. */
static int act_mate(World *w, Pool *p, int idx, const Settings *s, Rng *r)
{
    Creature *a = &p->items[idx];
    if (!can_mate(a, s)) {
        return -1;
    }
    /* mate_window is at most 65535 (the settings table), so this fits. */
    a->mate_wish = (uint16_t)s->mate_window;
    int partner = find_partner(w, p, a, s, r);
    if (partner < 0) {
        return -1;
    }
    if (s->max_plant_eaters > 0 && creature_counts_as_plant_eater(a, s) &&
        p->plant_eaters >= s->max_plant_eaters) {
        return -1; /* too many plant-eaters already */
    }
    Creature *b = &p->items[partner];
    int start = (int)rng_below(r, 8);
    int cell = free_cell_around(w, a, start);
    if (cell < 0) {
        cell = free_cell_around(w, b, start);
    }
    if (cell < 0) {
        return -1; /* no room: nobody pays, and they can try again */
    }
    int child_idx = pool_spawn(p, w, cell % w->w, cell / w->w);
    if (child_idx < 0) {
        /* The cell is free soil, so only a full pool can have said no. */
        p->births_refused++;
        return -1;
    }
    Creature *child = &p->items[child_idx];
    make_child(child, a, b, s, r);
    /* Read after the crossing: a child can differ from both parents. */
    if (creature_counts_as_plant_eater(child, s)) {
        p->plant_eaters++;
    }
    return child_idx;
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
    /* The season's drive: no choice needed. ACT_MATE below would only try a
     * second time in one tick, so it is left to the switch. */
    if (p->in_season && a.kind != ACT_MATE) {
        int child = act_mate(w, p, idx, s, r);
        if (child >= 0) {
            return child;
        }
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
        act_bite(w, p, idx, s, r);
        break;
    case ACT_MATE:
        return act_mate(w, p, idx, s, r);
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
    if (c->mate_wish > 0) {
        c->mate_wish--;
    }
    /* max_age is an int and age a uint32_t: comparing them directly would
     * convert the int to unsigned (a negative one to a huge number), which
     * -Wextra warns about. Checking > 0 first makes the cast safe. */
    if (s->max_age > 0 && c->age >= (uint32_t)s->max_age) {
        return false;
    }
    return c->energy > 0.0f;
}
