#include "creature.h"

#include <math.h>   /* powf, floorf, ceilf, lroundf, sqrtf */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* memset */

#include "chronicle.h" /* chronicle_note, LIFE_ kinds */
#include "climate.h" /* climate_region, climate_region_hue */
#include "flora.h" /* carrion_add, CARRION_MIN, plant_gives_shade */
#include "tactics.h" /* tactics_note_hit */
#include "years.h" /* SEASON_SUMMER */

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
    pool_forget_kinship(p); /* ids start again from 0 */
    /* A new pool starts at noon, in full light, until the Sim says otherwise:
     * a pool made by hand (a test, a tool) would otherwise be at light 0, and
     * every creature in it near blind. */
    p->light = 1.0f;
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

/* One remembered kinship: the pair of ids, the lower first, and the answer. */
typedef struct {
    uint32_t lo, hi;
    float kin;
    bool used;
} KinEntry;

/* A table of 2^16 entries, each pair of ids always in the same one (its hash),
 * a newer pair overwriting an older: 1 MB, enough for every pair of
 * neighbours in a crowded world. A struct with a name, so creature.h can hold
 * a pointer to it without saying what is inside. */
#define KIN_CACHE_BITS 16
struct KinCache {
    KinEntry entry[1 << KIN_CACHE_BITS];
};

void pool_free(Pool *p)
{
    free(p->items);
    free(p->kin_cache); /* free(NULL) does nothing */
    *p = (Pool){0};
}

bool pool_cache_kinship(Pool *p)
{
    if (p->kin_cache == NULL) {
        /* calloc zeroes what it gives: every entry starts unused. */
        p->kin_cache = calloc(1, sizeof(*p->kin_cache));
    }
    return p->kin_cache != NULL;
}

void pool_forget_kinship(Pool *p)
{
    if (p->kin_cache != NULL) {
        memset(p->kin_cache, 0, sizeof(*p->kin_cache));
    }
}

float pool_kinship(const Pool *p, const Creature *a, const Creature *b)
{
    /* p is const, but what kin_cache points to is not: a const struct holds a
     * pointer it may not change, while the memory behind that pointer may
     * change freely. The cache never changes an answer, only how fast it
     * comes, so the pool still looks the same to every reader. */
    struct KinCache *cache = p->kin_cache;
    if (cache == NULL) {
        return genome_kinship(&a->genome, &b->genome);
    }
    uint32_t lo = a->id < b->id ? a->id : b->id;
    uint32_t hi = a->id < b->id ? b->id : a->id;
    /* Multiplying by a large odd constant mixes the bits of the pair; the top
     * KIN_CACHE_BITS bits of the product pick the entry. */
    uint64_t key = ((uint64_t)hi << 32) | lo;
    uint64_t mixed = key * 0x9E3779B97F4A7C15ull;
    KinEntry *e = &cache->entry[mixed >> (64 - KIN_CACHE_BITS)];
    if (e->used && e->lo == lo && e->hi == hi) {
        return e->kin;
    }
    float kin = genome_kinship(&a->genome, &b->genome);
    *e = (KinEntry){lo, hi, kin, true};
    return kin;
}

int pool_spawn(Pool *p, World *w, int x, int y)
{
    int cell = world_idx(w, x, y);
    if (p->free_head < 0 || !world_walkable(w, cell) || w->occupant[cell] >= 0) {
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

/* Whether a creature could step onto this cell: ground it can stand on, with
 * nobody on it. */
static bool cell_is_free(const World *w, int cell)
{
    return world_walkable(w, cell) && w->occupant[cell] < 0;
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

float creature_vision(const Creature *c, float light, const Settings *s)
{
    float eyes = c->genome.g[GENE_NIGHT_EYES];
    float seen = clamp01(light + (1.0f - light) * eyes * s->night_eye_power);
    return seen * clamp01(1.0f - s->night_glare * eyes * light);
}

int creature_sight_now(const Creature *c, float light, const Settings *s)
{
    float cells = (float)creature_sight(c, s) * creature_vision(c, light, s);
    int now = (int)(cells + 0.5f);
    return now > 1 ? now : 1;
}

float creature_max_health(const Creature *c, const Settings *s)
{
    return s->health_per_size * creature_size(c, s);
}

/* See creature.h. Floats, so a huge age gives a huge result and not a wrapped
 * one. */
float creature_aged(const Creature *c, const Settings *s)
{
    float gene = c->genome.g[GENE_AGEING];
    float rate = s->ageing_min_rate + (1.0f - s->ageing_min_rate) * gene;
    return (float)c->age * rate / s->ageing_span;
}

/* What age costs this creature per tick. e is how far it has aged
 * (creature_aged). Squared, so that the cost is small for most of a life and
 * then climbs until food cannot pay for it: a practical end set by genes and
 * food, not a tick that kills a healthy creature. Slow ageing is paid for from
 * birth. */
static float ageing_cost(const Creature *c, const Settings *s)
{
    float gene = c->genome.g[GENE_AGEING];
    float e = creature_aged(c, s);
    return s->ageing_upkeep * e * e + s->slow_ageing_upkeep * (1.0f - gene);
}

float creature_upkeep_cost(const Creature *c, const Settings *s)
{
    float digest = c->genome.g[GENE_LEAF] + c->genome.g[GENE_FRUIT] +
                   c->genome.g[GENE_MEAT];
    return s->upkeep_base + s->size_upkeep * creature_size(c, s) +
           s->speed_upkeep * creature_speed(c, s) +
           s->sight_upkeep * (float)creature_sight(c, s) +
           s->digest_upkeep * digest * digest + ageing_cost(c, s) +
           s->armour_upkeep * c->genome.g[GENE_ARMOUR] +
           s->weapon_upkeep * c->genome.g[GENE_WEAPON] +
           s->territory_upkeep * c->genome.g[GENE_TERRITORY] +
           s->night_eye_upkeep * c->genome.g[GENE_NIGHT_EYES] +
           s->learn_upkeep * c->genome.g[GENE_LEARN] +
           s->place_memory_upkeep * c->genome.g[GENE_PLACE_MEMORY];
}

int creature_place_span(const Creature *c, const Settings *s)
{
    return (int)(c->genome.g[GENE_PLACE_MEMORY] * (float)s->place_memory_max + 0.5f);
}

float creature_region_cost(const World *w, const Creature *c, const Settings *s)
{
    int region = climate_region(w, world_idx(w, c->x, c->y), s);
    if (region == REGION_COLD) {
        return s->cold_upkeep / creature_size(c, s);
    }
    if (region == REGION_HOT_DRY) {
        return s->heat_upkeep * creature_size(c, s);
    }
    return 0.0f;
}

bool creature_in_shade(const World *w, const Creature *c, const Settings *s)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            /* world_idx wraps both axes, so the edge of the world has no edge. */
            int cell = world_idx(w, c->x + dx, c->y + dy);
            if (w->veg[cell] > 0 && plant_gives_shade(&w->plant[cell], s)) {
                return true;
            }
        }
    }
    return false;
}

bool creature_asleep(const Creature *c)
{
    return c->current.kind == ACT_SLEEP;
}

float creature_heat_cost(const World *w, const Creature *c, const Settings *s,
                         int season)
{
    if (season != SEASON_SUMMER || creature_in_shade(w, c, s)) {
        return 0.0f;
    }
    /* A hibernating body makes little heat of its own, and pays the same small
     * share of the sun as of its upkeep. A nap saves nothing here. */
    float share = creature_asleep(c) && c->hibernating ? s->sleep_upkeep : 1.0f;
    return s->summer_heat_upkeep * share;
}

float creature_step_extra(const World *w, const Creature *c, int cell,
                          const Settings *s)
{
    float legs = c->genome.g[GENE_LEGS];
    float swim = c->genome.g[GENE_SWIM];
    int region = climate_region(w, cell, s);
    if (region == REGION_SHALLOW) {
        return s->rough_step_cost * (1.0f - swim) + s->legs_step_cost * legs;
    }
    float on_land = s->swim_step_cost * swim;
    if (region == REGION_WET || region == REGION_HOT_DRY) {
        return s->rough_step_cost * (1.0f - legs) + on_land;
    }
    return s->legs_step_cost * legs + on_land;
}

/* How far apart two colours are, round the wheel: 0.98 and 0.01 are close. */
static float hue_distance(float a, float b)
{
    float d = fabsf(a - b);
    return d > 0.5f ? 1.0f - d : d;
}

float sight_reach(const Creature *seen, int seen_cell, const World *w,
                  const Settings *s)
{
    int region = climate_region(w, seen_cell, s);
    /* REGION_NONE is -1: used as an array index it would read outside the
     * table of colours, with nothing to stop it. */
    if (region == REGION_NONE) {
        return 1.0f;
    }
    float apart = hue_distance(seen->genome.g[GENE_HUE], climate_region_hue(region));
    float match = 1.0f - apart / s->camouflage_width;
    return match > 0.0f ? 1.0f - s->camouflage * match : 1.0f;
}

int creature_crowd(const World *w, const Creature *c, int radius)
{
    int own_cell = world_idx(w, c->x, c->y);
    int crowd = 0;
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int cell = world_idx(w, c->x + dx, c->y + dy);
            if (cell != own_cell && w->occupant[cell] >= 0) {
                crowd++;
            }
        }
    }
    return crowd;
}

float creature_crowd_cost(const World *w, const Creature *c, const Settings *s)
{
    if (s->crowd_upkeep <= 0.0f) {
        return 0.0f; /* off: spares looking at the cells at all */
    }
    int over = creature_crowd(w, c, s->crowd_radius) - s->crowd_free;
    return over > 0 ? s->crowd_upkeep * (float)over : 0.0f;
}

/* The energy at which this creature may mate: bigger bodies need more. */
static float mate_needs(const Creature *c, const Settings *s)
{
    return s->mate_threshold * creature_size(c, s);
}

bool creature_can_hibernate(const Creature *c, const Settings *s)
{
    return c->energy >= s->hibernate_energy * mate_needs(c, s);
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
    c->meal_tick += energy;
    c->since_meal = 0;
    float total = c->eaten + energy;
    c->level = 1.0f + (c->eaten * (c->level - 1.0f) + energy * source_level) /
                          total;
    c->eaten = total;
}

/* Whether a bite could take something from plant cell i right now, by the same
 * rules as plant_take and fruit_take below: a fruit tree offers no leaves, so
 * only a whole fruit; a leaf plant offers its leaves above the root, unless it
 * stands bare for the season. What the eater's own digestion would make of it
 * does not matter here: this is what the plant ray reports. */
static bool plant_has_food(const World *w, int i, const Settings *s, int season)
{
    const Plant *plant = &w->plant[i];
    if (w->veg[i] == 0) {
        return false;
    }
    if (plant_offers_fruit(plant)) {
        return plant->fruit >= 1.0f;
    }
    if (plant_is_bare(plant, season)) {
        return false;
    }
    /* veg is a uint8_t and plant_leaf_root an int: the uint8_t is promoted to
     * int before the comparison, so nothing wraps round. */
    return w->veg[i] > s->plant_leaf_root;
}

/* Whether c was hit in the last gang_window ticks. Unsigned subtraction gives
 * the true distance even across the wrap of the 32-bit date. */
static bool hit_lately(const Creature *c, uint32_t now, const Settings *s)
{
    if (c->hits_taken == 0) {
        return false;
    }
    const Hit *last = &c->recent_hits[(c->hits_taken - 1) % HITS_KEPT];
    return now - last->tick <= (uint32_t)s->gang_window;
}

/* The share of the 8 cells around c holding its own relatives. */
static float kin_share_around(const World *w, const Pool *p, const Creature *c,
                              const Settings *s)
{
    int kin = 0;
    for (int dir = 0; dir < 8; dir++) {
        int other = w->occupant[world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir])];
        if (other >= 0 &&
            pool_kinship(p, c, &p->items[other]) >= s->kin_sense) {
            kin++;
        }
    }
    return (float)kin / 8.0f;
}

/* Whether a relative of c within KIN_NEAR cells (a square) was hit lately. The
 * date is checked first: it is cheap, and kinship compares whole genomes. */
static bool kin_hit_near(const World *w, const Pool *p, const Creature *c,
                         const Settings *s)
{
    /* The square's wrapped columns and rows, worked out once: 14 wraps instead
     * of the 98 that world_idx would do for 49 cells. The square is
     * 2 * KIN_NEAR + 1 cells a side. */
    enum { SIDE = 2 * KIN_NEAR + 1 };
    int cols[SIDE], rows[SIDE];
    for (int k = 0; k < SIDE; k++) {
        cols[k] = ((c->x + k - KIN_NEAR) % w->w + w->w) % w->w;
        rows[k] = ((c->y + k - KIN_NEAR) % w->h + w->h) % w->h;
    }
    for (int i = 0; i < SIDE; i++) {
        for (int j = 0; j < SIDE; j++) {
            int other = w->occupant[rows[i] * w->w + cols[j]];
            if (other < 0 || &p->items[other] == c) {
                continue;
            }
            const Creature *o = &p->items[other];
            if (hit_lately(o, p->now, s) &&
                pool_kinship(p, c, o) >= s->kin_sense) {
                return true;
            }
        }
    }
    return false;
}

/* The clarity a view reaches 0 at, in sights: past 2.5 sights nothing is seen.
 * VIEW_FLOOR is what 1 / (1 + r * r) is there, 1 / (1 + 2.5 * 2.5). */
#define VIEW_REACH 2.5f
#define VIEW_FLOOR (1.0f / (1.0f + VIEW_REACH * VIEW_REACH))

float view_clarity(float d, float sight)
{
    float r = d / sight;
    /* 1 / (1 + r^2) falls smoothly from 1 and is 0.5 at one sight. Taking
     * VIEW_FLOOR off and stretching what is left back to 0..1 makes it end at
     * exactly 0 at VIEW_REACH sights instead of trailing on for ever, without
     * a step anywhere: the cut-off adds no edge. */
    float c = (1.0f / (1.0f + r * r) - VIEW_FLOOR) / (1.0f - VIEW_FLOOR);
    return c > 0.0f ? c : 0.0f;
}

float creature_view_sight(const Creature *c, float light, const Settings *s)
{
    float sight = (float)creature_sight(c, s) * creature_vision(c, light, s);
    return sight > 1.0f ? sight : 1.0f;
}

/* v moved back onto a ring of n cells. Cheaper than world_idx's two % (each a
 * division): one comparison, and an addition or subtraction. Right only for v
 * within one lap of the ring, -n <= v < 2n, which sense_view checks before
 * using it. */
static int wrap_near(int v, int n)
{
    if (v < 0) {
        return v + n;
    }
    return v >= n ? v - n : v;
}

/* The creature a view sees most clearly, while the view is walked. */
typedef struct {
    float clarity; /* 0 while none is seen */
    int idx;       /* its pool slot */
} Seen;

/* Fills in the details of the creature a view saw, each times how clearly it
 * is seen (VIEW_ in brain.h). */
static void describe_seen(const World *w, const Pool *p, const Creature *c,
                          const Settings *s, Seen seen, float *out)
{
    const Creature *o = &p->items[seen.idx];
    float clear = seen.clarity;
    float a = creature_size(c, s);
    float t = creature_size(o, s);
    out[VIEW_SEEN] = clear;
    out[VIEW_RELSIZE] = clear * t / (a + t);
    out[VIEW_KIN] = clear * pool_kinship(p, c, o);
    /* A bool converted to float is exactly 0.0f or 1.0f. */
    out[VIEW_HUNTER] = clear * (float)creature_is_hunter(o);
    out[VIEW_READY] = o->mate_wish > 0 ? clear : 0.0f;
    out[VIEW_ALIKE] = clear * genome_body_likeness(&c->genome, &o->genome);
    out[VIEW_FIGHTING] = hit_lately(o, p->now, s) ? clear : 0.0f;
    out[VIEW_ALONE] = clear * (1.0f - kin_share_around(w, p, o, s));
    out[VIEW_HURT] = clear * clamp01(1.0f - o->health / creature_max_health(o, s));
}

/*
 * One view, written into its VIEW_VALUES slots of out (brain.h says what each
 * means). The view is the wedge of ground around heading, walked outward one
 * step d at a time along view_lines fine lines fanned evenly from -0.75 to
 * 0.75 cells sideways per step: line j at step d is the cell d steps ahead
 * and round(slope_j * d) to the side, seen with the clarity of its true
 * distance, the square root of d^2 + side^2. A line stops at rock (water hides
 * nothing) and at a creature, which it sees. On a small map a line can come
 * round the wrap to the looker's own cell: the looker is not seen there and
 * hides nothing. Near the looker several lines
 * pass through the same cell; a cell is counted once, for the first line still
 * open that reaches it at that step. The lines are walked in order of slope, so
 * their sideways offsets never fall as j rises, and a repeat can only be the
 * same offset as the line before.
 */
static void sense_view(const World *w, const Pool *p, const Creature *c,
                       uint8_t heading, const Settings *s, float *out)
{
    int first = cell_towards(w, c->x, c->y, heading);
    /* The scent on that first cell, squashed into 0..1: half at one bite's
     * worth. The three views together say which way it grows. */
    float scent = w->scent[first];
    out[VIEW_CARRION] = scent / (scent + s->bite_meat_energy);

    float sight = creature_view_sight(c, p->light, s);
    /* ceilf rounds up: the last step at which clarity can still be above 0. */
    int reach = (int)ceilf(VIEW_REACH * sight);
    uint8_t side = turned(heading, 1); /* the step to its right */
    int lines = s->view_lines;
    /* A variable-length array would work here too, but its size would be set
     * only when the program runs, and nothing would stop a huge one from
     * overflowing the stack: a fixed top is safer. */
    bool open[VIEW_LINES_MAX];
    float slope[VIEW_LINES_MAX];
    for (int j = 0; j < lines; j++) {
        open[j] = true;
        slope[j] = lines == 1 ? 0.0f : -0.75f + 1.5f * (float)j / (float)(lines - 1);
    }
    /* The farthest a line goes from the looker along either axis is reach
     * cells (ahead) or 0.75 reach rounded (to the side). On a map at least that
     * wide either way, wrap_near is enough; on a smaller one world_idx does it. */
    bool small_map = reach >= w->w || reach >= w->h;
    float plants = 0.0f;
    Seen seen = {0.0f, -1};
    for (int d = 1; d <= reach; d++) {
        bool any_open = false;
        int last_offset = 0;
        bool last_valid = false; /* whether last_offset is a line's, this step */
        for (int j = 0; j < lines; j++) {
            if (!open[j]) {
                continue;
            }
            /* lroundf rounds half away from zero, the same on both sides, so
             * the fan is symmetric; it returns a long, cast to int (a few cells). */
            int offset = (int)lroundf(slope[j] * (float)d);
            bool repeat = last_valid && offset == last_offset;
            last_offset = offset;
            last_valid = true;
            int x = c->x + d * STEP_X[heading] + offset * STEP_X[side];
            int y = c->y + d * STEP_Y[heading] + offset * STEP_Y[side];
            int cell = small_map ? world_idx(w, x, y)
                                 : wrap_near(y, w->h) * w->w + wrap_near(x, w->w);
            if (w->terrain[cell] == TERRAIN_ROCK) {
                open[j] = false;
                continue;
            }
            int other = w->occupant[cell];
            if (other >= 0 && &p->items[other] == c) {
                other = -1; /* itself, come round the wrap */
            }
            if (other >= 0) {
                open[j] = false; /* the creature blocks what is behind it */
            } else {
                any_open = true;
            }
            if (repeat) {
                continue; /* counted already, by the line before */
            }
            bool food = plant_has_food(w, cell, s, p->season);
            if (!food && other < 0) {
                continue; /* nothing here to see: no need for its clarity */
            }
            /* sqrtf is the float square root; d and offset are ints, converted
             * to float before multiplying. */
            float distance = sqrtf((float)(d * d + offset * offset));
            float clear = view_clarity(distance, sight);
            if (food) {
                plants += clear;
            }
            if (other >= 0) {
                /* A creature matching the ground is seen less clearly, though
                 * it still hides what is behind it. */
                float how = clear * sight_reach(&p->items[other], cell, w, s);
                if (how > seen.clarity) {
                    seen = (Seen){how, other};
                }
            }
        }
        if (!any_open) {
            break; /* every line has stopped */
        }
    }
    out[VIEW_PLANT] = plants / (plants + s->view_half);
    if (seen.idx >= 0) {
        describe_seen(w, p, c, s, seen, out);
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
        if (pool_kinship(p, c, o) >= s->kin_sense) {
            kin++;
        } else {
            strangers++;
        }
    }
    out->v[SENSE_KIN_NEAR] = (float)kin / 8.0f;
    out->v[SENSE_STRANGERS_NEAR] = (float)strangers / 8.0f;
    out->v[SENSE_KIN_HIT] = kin_hit_near(w, p, c, s) ? 1.0f : 0.0f;
}

/* The shorter way from a to b on a ring of n cells: the map wraps at its edges,
 * so a creature at x = 1 hears one at x = n - 1 two cells away. */
static int wrapped_delta(int a, int b, int n)
{
    int d = b - a;
    if (d > n / 2) {
        d -= n;
    } else if (d < -n / 2) {
        d += n;
    }
    return d;
}

/* The calls c hears: the loudest relative's, the side it comes from, and the
 * loudest stranger's (SENSE_CALL_, brain.h). */
/* Running loudest calls, as hear_calls finds them. */
typedef struct {
    float kin_loud, kin_side, stranger_loud;
    uint32_t kin_id; /* who made the loudest relative's call */
} Heard;

/* One call, against the loudest heard so far. */
static void hear_one(const World *w, const Pool *p, const Creature *c,
                     const Settings *s, const Caller *call, Heard *h)
{
    const Creature *who = &p->items[call->idx];
    /* Not itself, and not a slot that holds someone else by now. */
    if (who == c || !who->alive || who->id != call->id) {
        return;
    }
    int dx = wrapped_delta(c->x, call->x, w->w);
    int dy = wrapped_delta(c->y, call->y, w->h);
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int d = ax > ay ? ax : ay;
    if (d < 1 || d > s->call_reach) {
        return;
    }
    float loud = 1.0f - (float)(d - 1) / (float)s->call_reach;
    /* The cheap test first: kinship compares whole genomes, and a call quieter
     * than both heard so far cannot change anything. */
    if (loud < h->kin_loud && loud <= h->stranger_loud) {
        return;
    }
    if (pool_kinship(p, c, who) >= s->kin_sense) {
        /* Of two equally loud, the lower id: the answer must not depend on the
         * order the calls are looked at in, or sorting them would change the
         * world. Only the side can differ between them. */
        if (loud > h->kin_loud || (loud == h->kin_loud && who->id < h->kin_id)) {
            h->kin_loud = loud;
            h->kin_id = who->id;
            /* Right or left of the way it faces: the step to the right, dotted
             * with the way to the caller. */
            uint8_t right = turned(c->heading, 1);
            int towards = dx * STEP_X[right] + dy * STEP_Y[right];
            h->kin_side = towards > 0 ? 1.0f : towards < 0 ? 0.0f : 0.5f;
        }
    } else if (loud > h->stranger_loud) {
        h->stranger_loud = loud;
    }
}

/* The squares (CALL_BLOCK cells a side) within reach cells of v along one
 * direction of a map n cells long, as square numbers in out, each once. Returns
 * how many. The map wraps, and on a small map the reach can cover a square
 * twice, hence the check for repeats. */
static int squares_in_reach(int v, int reach, int n, int count, int *out)
{
    int found = 0;
    for (int d = -reach; d <= reach; d++) {
        /* ((v + d) % n + n) % n: C's % keeps the sign of the left side, so -1 % n
         * is -1, and adding n once puts it back on the map. */
        int cell = ((v + d) % n + n) % n;
        int square = cell / CALL_BLOCK;
        bool seen = false;
        for (int k = 0; k < found; k++) {
            seen = seen || out[k] == square;
        }
        if (!seen && found < count) {
            out[found] = square;
            found++;
        }
    }
    return found;
}

static void hear_calls(const World *w, const Pool *p, const Creature *c,
                       const Settings *s, Senses *out)
{
    Heard h = {0.0f, 0.0f, 0.0f, UINT32_MAX};
    if (p->call_start == NULL) {
        for (int k = 0; k < p->n_callers; k++) {
            hear_one(w, p, c, s, &p->callers[k], &h);
        }
    } else {
        /* A reach of at most 32 cells spans at most 2 * 32 / 8 + 2 squares. */
        int cols[12], rows[12];
        int nc = squares_in_reach(c->x, s->call_reach, w->w, 12, cols);
        int nr = squares_in_reach(c->y, s->call_reach, w->h, 12, rows);
        for (int i = 0; i < nr; i++) {
            for (int j = 0; j < nc; j++) {
                int b = rows[i] * p->call_cols + cols[j];
                for (int k = p->call_start[b]; k < p->call_start[b + 1]; k++) {
                    hear_one(w, p, c, s, &p->callers[k], &h);
                }
            }
        }
    }
    out->v[SENSE_CALL_KIN] = h.kin_loud;
    out->v[SENSE_CALL_KIN_SIDE] = h.kin_side;
    out->v[SENSE_CALL_STRANGER] = h.stranger_loud;
}

/* The five SENSE_PLACE_ senses (brain.h). */
static void sense_place(const World *w, const Pool *p, const Creature *c,
                        const Settings *s, Senses *out)
{
    int span = creature_place_span(c, s);
    if (!c->meal_known || span == 0) {
        return;
    }
    /* Unsigned subtraction: the true age even across the wrap of the date. */
    uint32_t age = p->now - c->meal_at;
    if (age >= (uint32_t)span) {
        return; /* faded */
    }
    int dx = wrapped_delta(c->x, c->meal_x, w->w);
    int dy = wrapped_delta(c->y, c->meal_y, w->h);
    if (dx == 0 && dy == 0) {
        return; /* standing on it */
    }
    float fresh = 1.0f - (float)age / (float)span;
    /* How far forward and how far to the right the place lies: the way there,
     * dotted with the step ahead and with the step to the right. */
    uint8_t right = turned(c->heading, 1);
    int forward = dx * STEP_X[c->heading] + dy * STEP_Y[c->heading];
    int side = dx * STEP_X[right] + dy * STEP_Y[right];
    int side_size = side < 0 ? -side : side;
    if (forward > 0 && forward >= side_size) {
        out->v[SENSE_PLACE_AHEAD] = fresh;
    } else if (side > 0) {
        out->v[SENSE_PLACE_RIGHT] = fresh;
    } else if (side < 0) {
        out->v[SENSE_PLACE_LEFT] = fresh;
    }
    int ax = dx < 0 ? -dx : dx;
    int ay = dy < 0 ? -dy : dy;
    int cells = ax > ay ? ax : ay;
    out->v[SENSE_PLACE_NEAR] = clamp01(1.0f - (float)cells / 32.0f);
    out->v[SENSE_PLACE_FRESH] = fresh;
}

/* Holds at 0 the senses blind_senses names (BLIND_ bits, brain.h). */
static void blind(int mask, Senses *out)
{
    if (mask & BLIND_DAY) {
        out->v[SENSE_DAY_SIN] = 0.0f;
        out->v[SENSE_DAY_COS] = 0.0f;
    }
    for (int view = 0; view < VIEW_COUNT; view++) {
        float *slots = &out->v[SENSE_VIEWS + view * VIEW_VALUES];
        if (mask & BLIND_FIGHTING) {
            slots[VIEW_FIGHTING] = 0.0f;
        }
        if (mask & BLIND_ALONE) {
            slots[VIEW_ALONE] = 0.0f;
        }
        if (mask & BLIND_PLANTS) {
            slots[VIEW_PLANT] = 0.0f;
        }
    }
    if (mask & BLIND_KIN_HIT) {
        out->v[SENSE_KIN_HIT] = 0.0f;
    }
    if (mask & BLIND_PLACE) {
        for (int k = SENSE_PLACE_LEFT; k <= SENSE_PLACE_FRESH; k++) {
            out->v[k] = 0.0f;
        }
    }
    if (mask & BLIND_HEARING) {
        out->v[SENSE_CALL_KIN] = 0.0f;
        out->v[SENSE_CALL_KIN_SIDE] = 0.0f;
        out->v[SENSE_CALL_STRANGER] = 0.0f;
    }
}

static void sense_all(const World *w, const Pool *p, int idx,
                      const Settings *s, Senses *out);

void creature_sense(const World *w, const Pool *p, int idx,
                    const Settings *s, Senses *out)
{
    sense_all(w, p, idx, s, out);
    blind(s->blind_senses, out);
}

/* Every sense, before any blinding. */
static void sense_all(const World *w, const Pool *p, int idx,
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
    /* 2 pi as a float constant: math.h's M_PI is not part of standard C. sinf
     * and cosf are the float versions; plain sin would work in doubles and
     * need a conversion back. */
    float angle = 6.28318531f * p->year_phase;
    out->v[SENSE_YEAR_SIN] = 0.5f + 0.5f * sinf(angle);
    out->v[SENSE_YEAR_COS] = 0.5f + 0.5f * cosf(angle);
    float day_angle = 6.28318531f * p->day_phase;
    out->v[SENSE_DAY_SIN] = 0.5f + 0.5f * sinf(day_angle);
    out->v[SENSE_DAY_COS] = 0.5f + 0.5f * cosf(day_angle);

    /* The drives. Hunger is the sense of energy the other way up. */
    out->v[SENSE_HUNGER] = 1.0f - out->v[SENSE_ENERGY];
    out->v[SENSE_PAIN] = clamp01(1.0f - c->health / creature_max_health(c, s));
    /* Both are converted to float before dividing: as ints, 75 / 150 would be
     * 0, not 0.5. */
    out->v[SENSE_FED] =
        clamp01(1.0f - (float)c->since_meal / (float)s->fed_fade_ticks);
    for (int m = 0; m < MEMORY_VALUES; m++) {
        out->v[SENSE_MEMORY + m] = c->memory[m];
    }
    sense_place(w, p, c, s, out);

    /* Asleep, it feels only its own body and the time of year: everything
     * outside it (the way ahead, who is beside it, the three views, food in
     * reach) stays at the 0 it started from. */
    if (creature_asleep(c)) {
        return;
    }
    /* The same test act_move makes, so the sense cannot disagree with walking. */
    int ahead = cell_towards(w, c->x, c->y, c->heading);
    out->v[SENSE_BLOCKED] = cell_is_free(w, ahead) ? 0.0f : 1.0f;
    sense_neighbours(w, p, c, s, out);
    hear_calls(w, p, c, s, out);

    BiteChoice bite = creature_bite_choice(w, p, idx, s);
    out->v[SENSE_REACH_FOOD] = bite.kind != BITE_NOTHING ? 1.0f : 0.0f;

    /* The directions of the three views, in VIEW_ order. */
    const uint8_t view_heading[VIEW_COUNT] = {
        c->heading, turned(c->heading, 3), turned(c->heading, 1)};
    for (int view = 0; view < VIEW_COUNT; view++) {
        /* A pointer into the middle of the array: the view's first slot.
         * sense_view writes out[0] .. out[VIEW_VALUES - 1] from there. */
        float *slots = &out->v[SENSE_VIEWS + view * VIEW_VALUES];
        sense_view(w, p, c, view_heading[view], s, slots);
    }
}

/* The heading a blocked creature walks off in: one side or the other by chance,
 * else the other side, else back the way it came. Its own heading if all are
 * shut. The random draw comes first, so it is the same however the cells lie. */
static uint8_t way_around(const World *w, const Creature *c, Rng *r)
{
    int first_side = rng_below(r, 2) == 0 ? 1 : 3; /* right or left */
    const int tries[3] = {first_side, 4 - first_side, 2};
    for (int k = 0; k < 3; k++) {
        uint8_t heading = turned(c->heading, tries[k]);
        if (cell_is_free(w, cell_towards(w, c->x, c->y, heading))) {
            return heading;
        }
    }
    return c->heading;
}

/* Walks one cell forward if the creature has a move point; without one, nothing
 * happens. A blocked way does not stop it: it turns to a free side and walks
 * there in the same tick, so no brain can stand pushing at a rock. The step is
 * paid for either way, even by a creature shut in on all sides. */
static void act_move(World *w, Creature *c, int idx, const Settings *s, Rng *r)
{
    if (c->move_points < 1.0f - MOVE_SLACK) {
        return;
    }
    c->move_points -= 1.0f;
    c->energy -= s->move_cost;
    int from = world_idx(w, c->x, c->y);
    int to = cell_towards(w, c->x, c->y, c->heading);
    if (!cell_is_free(w, to)) {
        c->heading = way_around(w, c, r);
        to = cell_towards(w, c->x, c->y, c->heading);
        if (!cell_is_free(w, to)) {
            return;
        }
    }
    c->energy -= creature_step_extra(w, c, to, s);
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
    const Genome *theirs = &p->items[other].genome;
    if (genome_kinship(&c->genome, theirs) >= s->family_peace) {
        return -1;
    }
    /* Nor is anyone of a similar colour: what a creature looks like is what can be
     * seen on screen, and two who look alike can be distant by their genes. */
    if (hue_distance(c->genome.g[GENE_HUE], theirs->g[GENE_HUE]) < s->colour_peace) {
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

/* Whether a hit landing now starts a fight: the first hit of its life, or the
 * first after more than FIGHT_GAP quiet ticks. Read before the hit joins the ring. */
static bool starts_fight(const Creature *target, uint32_t now)
{
    if (target->hits_taken == 0) {
        return true;
    }
    const Hit *last = &target->recent_hits[(target->hits_taken - 1) % HITS_KEPT];
    return now - last->tick > FIGHT_GAP;
}

/* The life stories' side of a landed hit, which took the target's health from
 * before to where it is now. The target's death itself is noted by the Sim's
 * accounting, where it dies. */
static void record_hit(Pool *p, Creature *c, Creature *target, float before,
                       const Settings *s)
{
    if (p->chronicle == NULL) {
        return;
    }
    if (starts_fight(target, p->now)) {
        chronicle_note(p->chronicle, target,
                       (LifeEvent){.tick = p->now, .kind = LIFE_ATTACKED,
                                   .other_id = c->id});
    }
    if (target->health <= 0.0f) {
        uint8_t kind = creature_is_hunter(target) ? LIFE_KILL_HUNTER : LIFE_KILL;
        chronicle_note(p->chronicle, c,
                       (LifeEvent){.tick = p->now, .kind = kind,
                                   .other_id = target->id});
        return;
    }
    float heavy = HEAVY_WOUND * creature_max_health(target, s);
    if (before >= heavy && target->health < heavy) {
        /* A share of an unhurt body's health, as a whole percentage. Converting
         * a float to an int cuts the fraction off; the value is 0 to 25 here. */
        float left = 100.0f * target->health / creature_max_health(target, s);
        chronicle_note(p->chronicle, target,
                       (LifeEvent){.tick = p->now, .kind = LIFE_WOUNDED,
                                   .other_id = c->id, .value = (int32_t)left});
    }
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

/* A bitten sleeper wakes: it rests with nothing left to hold, so its brain
 * chooses again on its next turn, this tick or the next. A creature already
 * awake is left doing what it was doing, so a second hit changes nothing. */
static void wake(Creature *c)
{
    if (creature_asleep(c)) {
        c->current.kind = ACT_REST;
        c->current.hold = 1;
        c->hold_left = 0;
        c->hibernating = 0;
    }
}

/* An attack on whatever stands ahead. It costs the same hit, miss or swing at
 * nothing. The target is only hurt: if its health reaches 0 it stops acting, and
 * the end-of-tick accounting kills it and leaves its body as carrion, so the
 * attacker gains nothing directly. chosen: the brain chose it (ACT_ATTACK), as
 * against the territory drive; only chosen hits are told to the tactic watch,
 * which must never take a reflex for a tactic. */
static void act_attack(const World *w, Pool *p, int idx, const Settings *s,
                       Rng *r, bool chosen)
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
        float before = target->health;
        target->health -= hit_damage(c, target, a, s);
        target->hurt_tick += before - target->health;
        record_hit(p, c, target, before, s); /* reads the ring before the hit */
        if (chosen) {
            tactics_note_hit(p->tactics, w, p, c, target, target->health <= 0.0f,
                             s);
        }
        note_hit(target, c->id, p->now);
        wake(target);
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
 * mouth can take, up to what the plant holds. A leaf plant standing bare for
 * the season (flora.h) has nothing to take. */
static int plant_take(const World *w, int i, const Creature *c, const Settings *s,
                      int season)
{
    if (plant_is_bare(&w->plant[i], season)) {
        return 0;
    }
    int mouth = bite_steps(creature_size(c, s));
    /* What a bite may reach: never the root, which regrows the plant. */
    int edible = (int)w->veg[i] - s->plant_leaf_root;
    if (edible <= 0) {
        return 0;
    }
    return edible < mouth ? edible : mouth;
}

/* The share of a food's energy a digestion gene of this value gets out of it:
 * from digest_floor (gene 0) to 1 (gene 1, with the other two genes at 0). A
 * strong other gene takes from what lies above the floor, never the floor. */
static float digest_eff(float gene, float other_a, float other_b, const Settings *s)
{
    float other = other_a > other_b ? other_a : other_b;
    float above = (1.0f - s->digest_floor) * gene * (1.0f - s->digest_tradeoff * other);
    return s->digest_floor + above;
}

static float leaf_eff(const Creature *c, const Settings *s)
{
    return digest_eff(c->genome.g[GENE_LEAF], c->genome.g[GENE_FRUIT],
                      c->genome.g[GENE_MEAT], s);
}

static float fruit_eff(const Creature *c, const Settings *s)
{
    return digest_eff(c->genome.g[GENE_FRUIT], c->genome.g[GENE_LEAF],
                      c->genome.g[GENE_MEAT], s);
}

static float meat_eff(const Creature *c, const Settings *s)
{
    return digest_eff(c->genome.g[GENE_MEAT], c->genome.g[GENE_LEAF],
                      c->genome.g[GENE_FRUIT], s);
}

static float meat_gain(const Creature *c, float take, const Settings *s)
{
    return take * meat_eff(c, s);
}

/* What one density step of this plant's leaves gives c: nothing from a fruit
 * tree, and less for a weak leaf gene. */
static float leaf_gain(const Creature *c, const Plant *plant, const Settings *s)
{
    if (plant_offers_fruit(plant)) {
        return 0.0f;
    }
    return s->bite_veg_energy * leaf_eff(c, s);
}

/* How many whole fruit a bite by c would take from plant cell i. Worked out in
 * floats and only then converted: fruit from a damaged save could be too large
 * for an int, and that conversion would be undefined. mouth is small, so
 * whichever is returned fits. */
static int fruit_take(const World *w, int i, const Creature *c, const Settings *s)
{
    float mouth = (float)bite_steps(creature_size(c, s));
    float whole = floorf(w->plant[i].fruit);
    return (int)(whole < mouth ? whole : mouth);
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

/* Eats what its mouth can take of the leaves on this cell. When that leaves
 * the cell bare, flora_step resets the cell's age on the next tick. Harm may
 * take the plant's energy to 0, and then it starts withering on the next tick. */
static void eat_leaves(World *w, Creature *c, int cell, const Settings *s,
                       int season)
{
    Plant *plant = &w->plant[cell];
    int take = plant_take(w, cell, c, s, season);
    /* veg is a uint8_t, promoted to int for the subtraction. The result is
     * between 0 and what it held, so the cast back loses nothing. */
    w->veg[cell] = (uint8_t)(w->veg[cell] - take);
    float energy = plant->energy - (float)take * s->plant_leaf_harm;
    plant->energy = energy < 0.0f ? 0.0f : energy;
    float gain = (float)take * leaf_gain(c, plant, s);
    c->energy += gain;
    creature_ate(c, gain, 1.0f); /* plants are level 1 */
}

/* Eats whole fruit. The plant paid for it when it was made, so nothing else
 * happens to the plant. A creature already carrying a seed keeps that one: if
 * a newer seed replaced it, a steady fruit-eater would never drop any. */
static void eat_fruit(World *w, Creature *c, int cell, const Settings *s, Rng *r)
{
    Plant *plant = &w->plant[cell];
    int take = fruit_take(w, cell, c, s);
    plant->fruit -= (float)take;
    float gain = (float)take * s->bite_fruit_energy *
                 fruit_eff(c, s);
    c->energy += gain;
    creature_ate(c, gain, 1.0f);
    if (c->gut_ticks == 0) {
        plant_make_seed(&c->gut_seed, plant, r, s);
        c->gut_ticks = plant_gut_ticks(&c->gut_seed, s);
    }
}

/* One tick of carrying a seed. On the last one it drops where the creature
 * stands, which sprouts only if that soil is bare. */
static void carry_seed(World *w, Creature *c, const Settings *s)
{
    if (c->gut_ticks == 0) {
        return;
    }
    c->gut_ticks--;
    if (c->gut_ticks == 0) {
        plant_sow(w, world_idx(w, c->x, c->y), &c->gut_seed, s->plant_dung_start);
    }
}

bool creature_is_hunter(const Creature *c)
{
    /* Against whichever plant food it digests better. */
    float leaf = c->genome.g[GENE_LEAF];
    float fruit = c->genome.g[GENE_FRUIT];
    return c->genome.g[GENE_MEAT] > (leaf > fruit ? leaf : fruit);
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
 * gives this creature the most energy, carrion first on a tie, and on one
 * plant fruit before leaves on a tie, since fruit costs the plant nothing; a bite
 * that would gain nothing does nothing. Hurting anyone takes ACT_ATTACK. */
BiteChoice creature_bite_choice(const World *w, const Pool *p, int idx,
                                const Settings *s)
{
    const Creature *c = &p->items[idx];
    int food_cell = -1;
    bool food_is_carrion = false;
    bool food_is_fruit = false;
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
                food_is_fruit = false;
            }
        }
        if (w->veg[cell] > 0) {
            float leaves = (float)plant_take(w, cell, c, s, p->season) *
                           leaf_gain(c, &w->plant[cell], s);
            float fruit = (float)fruit_take(w, cell, c, s) * s->bite_fruit_energy *
                          fruit_eff(c, s);
            float gain = fruit >= leaves ? fruit : leaves;
            if (gain > best_gain) {
                best_gain = gain;
                food_cell = cell;
                food_is_carrion = false;
                food_is_fruit = fruit >= leaves;
            }
        }
    }

    BiteChoice choice = {BITE_NOTHING, -1, false};
    if (food_cell >= 0) {
        choice.fruit = food_is_fruit;
        choice.kind = food_is_carrion ? BITE_CARRION : BITE_PLANT;
        choice.cell = food_cell;
    }
    return choice;
}

/* A mouth built for meat bites whatever stands in front of it: with
 * bite_strikes on, a hunter's bite at a creature an attack would strike is
 * that attack, cost and odds included, and counts as chosen, since the brain
 * chose to bite. Without it, the brain had to link "stranger ahead" to a
 * separate attack output, and almost never did (2026-11-10 hunt probe: 89 %
 * of attacks struck an empty cell). */
/* After a bite that gave gain energy from the food on cell: a meal worth at
 * least meal_place_share of what mating needs is remembered, place and tick. */
static void remember_meal(Creature *c, float gain, int cell, const World *w,
                          uint32_t now, const Settings *s)
{
    if (cell < 0 || gain < s->meal_place_share * mate_needs(c, s)) {
        return;
    }
    c->meal_x = cell % w->w;
    c->meal_y = cell / w->w;
    c->meal_at = now;
    c->meal_known = 1;
}

static void act_bite(World *w, Pool *p, int idx, const Settings *s, Rng *r)
{
    Creature *c = &p->items[idx];
    if (s->bite_strikes && creature_is_hunter(c) &&
        creature_attack_target(w, p, idx, s) >= 0) {
        act_attack(w, p, idx, s, r, true);
        return;
    }
    BiteChoice choice = creature_bite_choice(w, p, idx, s);
    float before = c->energy;
    switch (choice.kind) {
    case BITE_CARRION:
        eat_carrion(w, c, choice.cell, s);
        break;
    case BITE_PLANT:
        if (choice.fruit) {
            eat_fruit(w, c, choice.cell, s, r);
        } else {
            eat_leaves(w, c, choice.cell, s, p->season);
        }
        break;
    default:
        /* Nothing in reach: the effort is wasted, and it is paid for. */
        c->energy -= s->bite_empty_cost;
        break;
    }
    remember_meal(c, c->energy - before, choice.cell, w, p->now, s);
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

/* Whether the creature in slot other can be c's partner now. */
static bool is_partner_for(const Pool *p, const Creature *c, int other,
                           const Settings *s)
{
    if (other < 0) {
        return false; /* an empty cell */
    }
    const Creature *o = &p->items[other];
    /* On a world narrower than the search, a far cell can wrap round onto c
     * itself, and c has just made its wish: without this it would pair with
     * itself. Two pointers into one array are equal only for the same slot. */
    if (o == c) {
        return false;
    }
    /* A sleeper is no partner, whatever wish it fell asleep with. */
    return o->mate_wish > 0 && !creature_asleep(o) && can_mate(o, s) &&
           genome_body_likeness(&c->genome, &o->genome) >= s->mate_barrier;
}

/* The k-th cell, 0 <= k < 8 * d, of the square ring d steps from the centre,
 * walked clockwise from its top-left corner: top row, right side, bottom row,
 * left side, each side 2 * d cells long. Written to *dx and *dy. */
static void ring_cell(int d, int k, int *dx, int *dy)
{
    int side = k / (2 * d);
    int t = k % (2 * d);
    switch (side) {
    case 0: *dx = -d + t; *dy = -d; break;
    case 1: *dx = d; *dy = -d + t; break;
    case 2: *dx = d - t; *dy = d; break;
    default: *dx = -d; *dy = d - t; break;
    }
}

/* Weighs the creature in slot other as c's partner: if it is one, and more
 * related to c than the best so far (*best_kin), it becomes the best. Returns
 * true when the search can stop here: with mate_by_kin off, the first partner
 * found is the one. Only a strictly higher kinship replaces the best, so of two
 * as related the one found first, the nearer, stays. */
static bool weigh_partner(const Pool *p, const Creature *c, int other,
                          const Settings *s, int *best, float *best_kin)
{
    if (!is_partner_for(p, c, other, s)) {
        return false;
    }
    if (!s->mate_by_kin) {
        *best = other;
        return true;
    }
    float kin = pool_kinship(p, c, &p->items[other]);
    if (kin > *best_kin) {
        *best = other;
        *best_kin = kin;
    }
    return false;
}

/* The slot of a creature within mate_reach steps that wishes to mate and still
 * can; -1 for none. The 8 cells beside come first, then each wider ring. With
 * mate_by_kin off the first one found is chosen, so a nearer partner always wins
 * over a farther one; on, every cell in reach is weighed and the most related
 * wins (weigh_partner). Each ring is looked round from a random start so that no
 * side is favoured. The first ring is read in the AROUND order with the one draw
 * it always had, so a mate_reach of 1 draws the same numbers as before the
 * setting and gives the same worlds. With mate_by_kin on, every ring is drawn
 * for even when a partner was found beside: a different count of draws, so
 * different worlds, which a new rule gives anyway. */
static int find_partner(const World *w, const Pool *p, const Creature *c,
                        const Settings *s, Rng *r)
{
    int best = -1;
    /* Below any kinship (0 to 1), so the first partner weighed always counts. */
    float best_kin = -1.0f;
    int start = (int)rng_below(r, 8);
    for (int k = 0; k < 8; k++) {
        int dir = (start + k) % 8;
        int cell = world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir]);
        if (weigh_partner(p, c, w->occupant[cell], s, &best, &best_kin)) {
            return best;
        }
    }
    for (int d = 2; d <= s->mate_reach; d++) {
        int cells = 8 * d;
        /* cells is at most 32 (mate_reach is at most 4), so the cast is safe. */
        int from = (int)rng_below(r, (uint32_t)cells);
        for (int k = 0; k < cells; k++) {
            int dx, dy;
            ring_cell(d, (from + k) % cells, &dx, &dy);
            int cell = world_idx(w, c->x + dx, c->y + dy);
            if (weigh_partner(p, c, w->occupant[cell], s, &best, &best_kin)) {
                return best;
            }
        }
    }
    return best;
}

/* A free cell a body can stand on beside c, looking round from start; -1 for none. */
static int free_cell_around(const World *w, const Creature *c, int start)
{
    for (int k = 0; k < 8; k++) {
        int dir = (start + k) % 8;
        int cell = world_idx(w, c->x + AROUND_X[dir], c->y + AROUND_Y[dir]);
        if (cell_is_free(w, cell)) {
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

/* The life stories' side of a birth: the child's, and one event for each parent. */
static void record_birth(Pool *p, Creature *child, Creature *a, Creature *b)
{
    if (p->chronicle == NULL) {
        return;
    }
    child->tally.parents[0] = a->id;
    child->tally.parents[1] = b->id;
    chronicle_note(p->chronicle, child,
                   (LifeEvent){.tick = p->now, .kind = LIFE_BORN,
                               .other_id = a->id, .value = 1});
    LifeEvent had = {.tick = p->now, .kind = LIFE_CHILD, .other_id = child->id};
    chronicle_note(p->chronicle, a, had); /* had is copied in: a struct is */
    chronicle_note(p->chronicle, b, had); /* passed by value, like a number */
}

/* Whether a land with room for capacity creatures lets one more be born, with
 * alive already on it: chance 1 - alive / capacity, none at or above it. The
 * random number is drawn only when the answer is not already no. */
static bool room_on_the_land(int alive, float capacity, Rng *r)
{
    if (!(capacity > 0.0f)) {
        return false; /* no plants at all: no room (also catches NaN) */
    }
    /* alive is an int; the cast makes the division a float one. With plain
     * ints, alive / capacity would be 0 for every count below the capacity,
     * and the rule would never slow anything. */
    float full = (float)alive / capacity;
    return full < 1.0f && rng_unit(r) >= full;
}

/* Whether crowding lets a's pair have a child now, by the four birth rules in
 * settings.h (birth_room, birth_crowd_price, birth_capacity, birth_per_plant).
 * Each one that is off costs nothing: the crowd is only counted, and a random
 * number only drawn, when a rule needs it, so a world with all four off is the
 * world without them. */
static bool crowd_allows_birth(const World *w, const Pool *p, const Creature *a,
                               const Settings *s, Rng *r)
{
    if (s->birth_room > 0 || s->birth_crowd_price > 0.0f) {
        int crowd = creature_crowd(w, a, s->crowd_radius);
        if (s->birth_room > 0 && crowd > s->birth_room) {
            return false; /* no room to raise one */
        }
        int over = crowd - s->crowd_free;
        if (over > 0 &&
            a->energy < mate_needs(a, s) *
                            (1.0f + s->birth_crowd_price * (float)over)) {
            return false; /* too dear here */
        }
    }
    if (s->birth_capacity > 0 &&
        !room_on_the_land(p->count, (float)s->birth_capacity, r)) {
        return false;
    }
    if (s->birth_per_plant > 0.0f &&
        !room_on_the_land(p->count, s->birth_per_plant * (float)p->plants, r)) {
        return false;
    }
    return true;
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
    if (!crowd_allows_birth(w, p, a, s, r)) {
        return -1; /* nobody pays, and they can try again */
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
    record_birth(p, child, a, b);
    /* Read after the crossing: a child can differ from both parents. */
    if (creature_counts_as_plant_eater(child, s)) {
        p->plant_eaters++;
    }
    return child_idx;
}

/* The territorial drive: an adult may turn on a stranger on one of the four cells
 * beside it, looking round from a random heading so no side is favoured. True when
 * it attacked, which takes up its tick. Whoever counts as a stranger is decided by
 * creature_attack_target, the same test an attack it chose itself would meet, so
 * close relatives are safe here too. With the drive off, this draws no random
 * numbers at all, so the world is the same as if it were not here. */
static bool territorial_drive(const World *w, Pool *p, int idx, const Settings *s,
                              Rng *r)
{
    Creature *c = &p->items[idx];
    if (s->territory_drive <= 0.0f || c->age < (uint32_t)s->mate_age) {
        return false;
    }
    uint8_t own_heading = c->heading;
    uint8_t start = (uint8_t)rng_below(r, HEADING_COUNT);
    for (int k = 0; k < HEADING_COUNT; k++) {
        c->heading = turned(start, k);
        if (creature_attack_target(w, p, idx, s) >= 0) {
            float chance = s->territory_drive * c->genome.g[GENE_TERRITORY];
            if (rng_unit(r) < chance) {
                act_attack(w, p, idx, s, r, false); /* now facing it */
                return true;
            }
            break;
        }
    }
    c->heading = own_heading;
    return false;
}

int creature_act(World *w, Pool *p, int idx, Action a, const Settings *s,
                 Rng *r)
{
    Creature *c = &p->items[idx];
    carry_seed(w, c, s);
    /* Move points build up every tick, whatever the action, but never
     * past 1: a creature cannot save up for a burst of moves. */
    c->move_points += creature_speed(c, s);
    if (c->move_points > 1.0f) {
        c->move_points = 1.0f;
    }
    /* Deep sleep: nothing else happens, not even the season's drive or the
     * territorial one below. The seed above still travels and drops, and move
     * points still build up, as they do whatever the action. */
    if (a.kind == ACT_SLEEP) {
        return -1;
    }
    /* The season's drive: no choice needed. ACT_MATE below would only try a
     * second time in one tick, so it is left to the switch. */
    if (p->in_season && a.kind != ACT_MATE) {
        int child = act_mate(w, p, idx, s, r);
        if (child >= 0) {
            return child;
        }
    }
    /* A creature that has chosen to mate is not turned from it by this. */
    if (a.kind != ACT_MATE && territorial_drive(w, p, idx, s, r)) {
        return -1;
    }
    switch (a.kind) {
    case ACT_TURN_LEFT:
        c->heading = turned(c->heading, 3);
        break;
    case ACT_TURN_RIGHT:
        c->heading = turned(c->heading, 1);
        break;
    case ACT_MOVE:
        act_move(w, c, idx, s, r);
        break;
    case ACT_BITE:
        act_bite(w, p, idx, s, r);
        break;
    case ACT_ATTACK:
        act_attack(w, p, idx, s, r, true);
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

/* The share of its upkeep a creature pays this tick, by what it is doing. The
 * ageing part is included in the discount: a body at rest or napping costs
 * less, and a hibernating one far less. Ageing itself goes on at the same pace. */
static float upkeep_share(const Creature *c, const Settings *s)
{
    if (creature_asleep(c) && c->hibernating) {
        return s->sleep_upkeep;
    }
    return c->current.kind == ACT_REST || creature_asleep(c) ? s->rest_upkeep
                                                             : 1.0f;
}

bool creature_upkeep(Creature *c, const Settings *s)
{
    if (c->health <= 0.0f) {
        return false;
    }
    c->energy -= creature_upkeep_cost(c, s) * upkeep_share(c, s);
    if (c->age < UINT32_MAX) {
        c->age++;
    }
    if (creature_asleep(c) && c->sleep_ticks < UINT16_MAX) {
        c->sleep_ticks++;
    }
    if (c->since_meal < UINT16_MAX) {
        c->since_meal++;
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
