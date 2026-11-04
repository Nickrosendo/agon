#include "creature.h"

#include <math.h>   /* powf */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* memset */

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
    /* The struct's array itself, so this clears every cause, however many. */
    memset(p->deaths, 0, sizeof(p->deaths));
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
        .health = 1.0f,
    };
    /* An unsigned counter: after 2^32 - 1 it wraps to 0, which is defined
     * in C (unlike signed overflow). At 1,000 births a second that takes
     * over 49 days. */
    p->next_id++;
    p->count++;
    w->occupant[cell] = idx;
    return idx;
}

void pool_kill(Pool *p, World *w, int idx, const Settings *s, DeathCause cause)
{
    Creature *c = &p->items[idx];
    if (!c->alive) {
        return;
    }
    int cell = world_idx(w, c->x, c->y);
    /* The body, plus whatever energy was left: a starved creature has none,
     * one killed by wounds may have plenty, and all of it lands on the cell. */
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
    p->deaths[cause]++;
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
    return s->size_min + gene * (s->birth_size_max - s->size_min) + c->grown;
}

int bite_steps(float size)
{
    /* Compared as a float before converting: converting a float too large for
     * int is undefined behaviour, and size has no top. */
    if (size >= (float)VEG_MAX) {
        return VEG_MAX;
    }
    int steps = (int)(size + 0.5f);
    return steps < 1 ? 1 : steps;
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

float creature_max_health(const Creature *c, const Settings *s)
{
    return s->health_per_size * creature_size(c, s);
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
           s->digest_upkeep * digest * digest + ageing_cost(c, s) +
           s->armour_upkeep * c->genome.g[GENE_ARMOUR] +
           s->weapon_upkeep * c->genome.g[GENE_WEAPON];
}

/* The energy at which this creature may mate: bigger bodies need more. */
static float mate_needs(const Creature *c, const Settings *s)
{
    return s->mate_threshold * creature_size(c, s);
}

float attack_chance(float a, float t, float k)
{
    return attack_odds(a, t, k, 0, 0.0f);
}

float attack_odds(float a, float t, float k, int others_recently,
                  float gang_bonus)
{
    /* a^k / (a^k + t^k), divided through by a^k. Raising the ratio instead of
     * each size keeps this a real number for sizes far apart: 1000000^16
     * overflows a float to infinity, and infinity / infinity is NaN, which a
     * comparison with a random draw would quietly read as a miss for ever. */
    float t_over_a = 1.0f;
    if (a > 0.0f || t > 0.0f) { /* both 0 would divide 0 by 0: then, even odds */
        t_over_a = powf(t / a, k);
    }
    float weight = 1.0f + (float)others_recently * gang_bonus;
    return weight / (weight + t_over_a);
}

int creature_others_recently(const Creature *target, uint32_t attacker,
                             uint32_t now, int window)
{
    uint32_t kept = target->hits_taken < HITS_KEPT ? target->hits_taken
                                                   : HITS_KEPT;
    int others = 0;
    for (uint32_t i = 0; i < kept; i++) {
        const Hit *h = &target->recent_hits[i];
        /* Unsigned subtraction wraps modulo 2^32, so this is the true distance
         * in ticks even if the counter passed 2^32 between the two. window is
         * checked to be 0 or more when settings load. */
        if (h->by == attacker || now - h->tick > (uint32_t)window) {
            continue;
        }
        /* Seen already, earlier in the ring? Repeats by one creature count once. */
        bool repeat = false;
        for (uint32_t j = 0; j < i; j++) {
            const Hit *e = &target->recent_hits[j];
            if (e->by == h->by && now - e->tick <= (uint32_t)window) {
                repeat = true;
            }
        }
        if (!repeat) {
            others++;
        }
    }
    return others;
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

/* The shares of the 8 cells around holding relatives and strangers. Who counts
 * as a relative is decided by how alike the genomes are, not by any family
 * name: all a creature can have to go on is what it can tell by looking. */
static void sense_neighbours(const World *w, const Pool *p, const Creature *c,
                             const Settings *s, Senses *out)
{
    int kin = 0, strangers = 0;
    for (int dir = 0; dir < 8; dir++) {
        int cell = world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir]);
        int other = w->occupant[cell];
        if (other < 0) {
            continue;
        }
        const Creature *o = &p->items[other];
        if (genome_kinship(&c->genome, &o->genome) >= s->kin_sense) {
            kin++;
        } else {
            strangers++;
        }
    }
    out->v[SENSE_KIN_NEAR] = (float)kin / 8.0f;
    out->v[SENSE_STRANGERS_NEAR] = (float)strangers / 8.0f;
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
    /* The same shape as age: 0.5 at size 1, and below 1 at any size, however
     * large, since size has no top. */
    float size = creature_size(c, s);
    out->v[SENSE_SIZE] = size / (size + 1.0f);
    out->v[SENSE_DIET] = c->genome.g[GENE_MEAT]; /* how much a hunter */
    out->v[SENSE_SEASON] = p->in_season ? 1.0f : 0.0f;
    /* The same test act_move makes, so the sense cannot disagree with walking. */
    int ahead = cell_towards(w, c->x, c->y, c->heading);
    out->v[SENSE_BLOCKED] =
        w->terrain[ahead] != TERRAIN_SOIL || w->occupant[ahead] >= 0 ? 1.0f : 0.0f;

    /* The drives. Hunger is the sense of energy the other way up. */
    out->v[SENSE_HUNGER] = 1.0f - out->v[SENSE_ENERGY];
    out->v[SENSE_PAIN] = clamp01(1.0f - c->health / creature_max_health(c, s));
    sense_neighbours(w, p, c, s, out);
    for (int m = 0; m < MEMORY_VALUES; m++) {
        out->v[SENSE_MEMORY + m] = c->memory[m];
    }

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

int creature_attack_target(const World *w, const Pool *p, int idx,
                           const Settings *s)
{
    const Creature *c = &p->items[idx];
    int other = w->occupant[cell_towards(w, c->x, c->y, c->heading)];
    /* other == idx only in a world one cell across, where ahead wraps to itself. */
    if (other < 0 || other == idx || p->items[other].health <= 0.0f) {
        return -1;
    }
    /* Close relatives are not struck. The swing is still paid for, so choosing
     * to attack beside them costs energy and selection can act on that. */
    if (genome_kinship(&c->genome, &p->items[other].genome) >= s->family_peace) {
        return -1;
    }
    return other;
}

/* Adds a hit to the target's ring, over its oldest once the ring is full. */
static void note_hit(Creature *target, uint32_t by, uint32_t now)
{
    target->recent_hits[target->hits_taken % HITS_KEPT] = (Hit){by, now};
    target->hits_taken++;
}

/* What one landed hit takes from the target, at attacker size a. A share of it
 * is blocked rather than an amount, so armour never makes anyone untouchable at
 * the default setting. The floor is for an armour_block above 1, where the
 * share would go negative and a hit would heal. */
static float hit_damage(const Creature *c, const Creature *target, float a,
                        const Settings *s)
{
    float raised = 1.0f + s->weapon_damage * c->genome.g[GENE_WEAPON];
    float left = 1.0f - s->armour_block * target->genome.g[GENE_ARMOUR];
    return s->bite_damage * a * raised * (left > 0.0f ? left : 0.0f);
}

/* An attack on whatever stands ahead. It costs the same hit, miss or swing at
 * nothing. The target is only hurt: if its health reaches 0 it stops acting, and
 * the end-of-tick accounting kills it and leaves its body as carrion, so the
 * attacker gains nothing directly. */
static void act_attack(const World *w, Pool *p, int idx, const Settings *s,
                       Rng *r)
{
    Creature *c = &p->items[idx];
    c->energy -= s->attack_cost;
    int slot = creature_attack_target(w, p, idx, s);
    if (slot < 0) {
        return;
    }
    Creature *target = &p->items[slot];
    float a = creature_size(c, s);
    int others = creature_others_recently(target, c->id, p->now, s->gang_window);
    float chance = attack_odds(a, creature_size(target, s), s->attack_steepness,
                               others, s->gang_bonus);
    if (rng_unit(r) < chance) {
        target->health -= hit_damage(c, target, a, s);
        note_hit(target, c->id, p->now);
    }
}

/* How much one bite of carrion on cell i would take. A larger mouth takes more. */
static float carrion_take(const World *w, int i, const Creature *c,
                          const Settings *s)
{
    float mouth = s->bite_meat_energy * creature_size(c, s);
    return w->carrion[i] < mouth ? w->carrion[i] : mouth;
}

/* How many density steps a bite by c would take from plant cell i: what its
 * mouth can take, up to what the plant holds. */
static int plant_take(const World *w, int i, const Creature *c, const Settings *s)
{
    int mouth = bite_steps(creature_size(c, s));
    return w->veg[i] < mouth ? w->veg[i] : mouth;
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

/* What one density step of a plant with the given rule gives c: less for a
 * vigorous rule (veg_vigour_food), less for a weak plant gene. */
static float plant_gain(const Creature *c, uint8_t rule, const Settings *s)
{
    float food = 1.0f - s->veg_vigour_food * veg_rule_vigour(rule);
    return s->bite_veg_energy * food * digest_eff(c->genome.g[GENE_PLANT], s);
}

/* Eats what its mouth can take of the carrion ahead. */
static void eat_carrion(World *w, Creature *c, int ahead, const Settings *s)
{
    float take = carrion_take(w, ahead, c, s);
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

/* Eats what its mouth can take of the vegetation ahead. When that leaves
 * the cell bare, flora_step resets the cell's age on the next tick. */
static void eat_plant(World *w, Creature *c, int ahead, const Settings *s)
{
    int take = plant_take(w, ahead, c, s);
    /* veg is a uint8_t, promoted to int for the subtraction. The result is
     * between 0 and what it held, so the cast back loses nothing. */
    w->veg[ahead] = (uint8_t)(w->veg[ahead] - take);
    float gain = (float)take * plant_gain(c, w->veg_rule[ahead], s);
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
 * that would gain nothing does nothing. Hurting anyone takes ACT_ATTACK. */
BiteChoice creature_bite_choice(const World *w, const Pool *p, int idx,
                                const Settings *s)
{
    const Creature *c = &p->items[idx];
    int food_cell = -1;
    bool food_is_carrion = false;
    float best_gain = 0.0f;

    /* AROUND_ is clockwise from north, two steps per heading. */
    int start = 2 * c->heading;
    for (int k = 0; k < 8; k++) {
        int dir = (start + k) % 8;
        int cell = world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir]);
        if (w->occupant[cell] >= 0) {
            continue; /* the food under a creature is covered */
        }
        if (w->carrion[cell] > 0.0f) {
            float gain = meat_gain(c, carrion_take(w, cell, c, s), s);
            if (gain > best_gain) {
                best_gain = gain;
                food_cell = cell;
                food_is_carrion = true;
            }
        }
        if (w->veg[cell] > 0) {
            float gain = (float)plant_take(w, cell, c, s) *
                         plant_gain(c, w->veg_rule[cell], s);
            if (gain > best_gain) {
                best_gain = gain;
                food_cell = cell;
                food_is_carrion = false;
            }
        }
    }

    BiteChoice choice = {BITE_NOTHING, -1};
    if (food_cell >= 0) {
        choice.kind = food_is_carrion ? BITE_CARRION : BITE_PLANT;
        choice.cell = food_cell;
    }
    return choice;
}

static void act_bite(World *w, Pool *p, int idx, const Settings *s)
{
    Creature *c = &p->items[idx];
    BiteChoice choice = creature_bite_choice(w, p, idx, s);
    switch (choice.kind) {
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

void creature_start_family(Creature *c)
{
    c->lineage = c->id;
    c->family_hue = c->genome.g[GENE_HUE];
}

/* Grown up and fed enough to have a child. mate_age is an int checked to be
 * 0 or more when settings load, so the cast cannot make a huge number of it.
 * A creature an attack has just taken to 0 is dying, and must not become a
 * parent; the check for energy above 0 is for a mate_threshold of 0. */
static bool can_mate(const Creature *c, const Settings *s)
{
    return c->age >= (uint32_t)s->mate_age && c->health > 0.0f &&
           c->energy > 0.0f && c->energy >= mate_needs(c, s);
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

/* How far apart two colours are, round the wheel: 0.98 and 0.01 are close. */
static float hue_distance(float a, float b)
{
    float d = fabsf(a - b);
    return d > 0.5f ? 1.0f - d : d;
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
    child->heading = (uint8_t)rng_below(r, HEADING_COUNT);
    child->level = (a->level + b->level) / 2.0f; /* until its first meal */
    /* Pointers to the three genomes, not copies: the function writes the
     * child's in place, reading its parents'. */
    genome_cross(&child->genome, &a->genome, &b->genome, r, s);
    child->health = creature_max_health(child, s); /* needs the genes above */
    /* It joins the family whose colour it is closer to. Only then is any distance
     * left real drift: naming it at random could give it one parent's colour and
     * the other's family, and two-coloured parents would found a family per birth. */
    float hue = child->genome.g[GENE_HUE];
    float a_off = hue_distance(hue, a->family_hue);
    float b_off = hue_distance(hue, b->family_hue);
    const Creature *family = a_off <= b_off ? a : b;
    child->lineage = family->lineage;
    child->family_hue = family->family_hue;
    if (hue_distance(hue, child->family_hue) > s->family_split) {
        creature_start_family(child);
    }
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
        act_bite(w, p, idx, s);
        break;
    case ACT_ATTACK:
        act_attack(w, p, idx, s, r);
        break;
    case ACT_MATE:
        return act_mate(w, p, idx, s, r);
    default:
        break; /* ACT_REST, or a kind no brain should send */
    }
    return -1;
}

/* Heals one tick's worth, or what is left of the wound, or what energy can pay
 * for, whichever is least. Called only with energy above 0. */
static void heal(Creature *c, const Settings *s)
{
    float amount = creature_max_health(c, s) - c->health;
    if (amount > s->heal_rate) {
        amount = s->heal_rate;
    }
    /* The check also keeps a heal_cost of 0 from being divided by. */
    if (s->heal_cost > 0.0f && amount * s->heal_cost > c->energy) {
        amount = c->energy / s->heal_cost;
    }
    if (amount > 0.0f) {
        c->health += amount;
        c->energy -= amount * s->heal_cost;
    }
}

/* Turns the growth gene's share of any surplus energy into body. Surplus is
 * energy above growth_surplus * what mating needs, so a creature always keeps
 * enough to mate. growth_cost is at least a small number, by the table's check. */
static void grow(Creature *c, const Settings *s)
{
    float surplus = c->energy - s->growth_surplus * mate_needs(c, s);
    if (surplus <= 0.0f) {
        return;
    }
    float spent = surplus * c->genome.g[GENE_GROWTH];
    c->energy -= spent;
    c->grown += spent / s->growth_cost;
}

bool creature_upkeep(Creature *c, const Settings *s)
{
    if (c->health <= 0.0f) {
        return false;
    }
    /* The ageing part is included in the discount: a body at rest costs less. */
    float share = c->current.kind == ACT_REST ? s->rest_upkeep : 1.0f;
    c->energy -= creature_upkeep_cost(c, s) * share;
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
    if (c->energy > 0.0f) {
        heal(c, s);
        grow(c, s);
    } else {
        /* Exactly 0, not below: a debt would have to be eaten off before the
         * first meal did any good, and starving is already the price. */
        c->energy = 0.0f;
        c->health -= s->starve_damage;
    }
    return c->health > 0.0f;
}
