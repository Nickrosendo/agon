#include "sim.h"

#include <stddef.h> /* size_t */
#include <stdlib.h> /* malloc, free */

#include "flora.h"
#include "terrain.h"

/* How many random cells to try when placing one creature before giving
 * up (the world may have little free soil left). */
#define PLACE_ATTEMPTS 64

/* Mixed into the seed for the brain, so the brain's random numbers are
 * not the same series as the world's. Any odd 64-bit constant would do;
 * this one is the fractional part of the golden ratio. */
#define BRAIN_SEED_SALT 0x9E3779B97F4A7C15ULL

/* Random starting plants: two draws per soil cell, in row order, so that the
 * count of draws does not depend on what the draws decide. Every starting
 * plant carries the rule from the settings. Each gets a random age up to the
 * age of maturity: all born on one tick, they would all wither together. */
static void plant_start(World *w, Rng *r, const Settings *s)
{
    uint8_t rule =
        veg_rule_pack(s->veg_birth_lo, s->veg_birth_hi, s->veg_crowd_max);
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->terrain[i] == TERRAIN_SOIL) {
            w->veg[i] = rng_unit(r) < s->veg_start_chance ? 1 : 0;
            w->veg_rule[i] = rule;
            /* veg_mature_age is at most 65535 (the settings table), so it
             * fits; + 1u keeps rng_below from being asked for 0 values. */
            uint32_t age = rng_below(r, (uint32_t)s->veg_mature_age + 1u);
            w->veg_age[i] = w->veg[i] > 0 ? (uint16_t)age : 0;
        }
    }
}

/* Places one creature on a random free soil cell with the starting
 * energy, a random heading and a random genome, and tells the brain.
 * Returns its slot, or -1 if the pool is full or no free cell turned
 * up. */
static int place_creature(Sim *s)
{
    if (s->pool.count >= s->pool.capacity) {
        return -1; /* full: spend no random draws */
    }
    for (int attempt = 0; attempt < PLACE_ATTEMPTS; attempt++) {
        /* Two statements, so the draws happen in a fixed order (x
         * first). Inside one function call's arguments the order would
         * be up to the compiler. */
        int x = (int)rng_below(&s->rng, (uint32_t)s->world.w);
        int y = (int)rng_below(&s->rng, (uint32_t)s->world.h);
        int idx = pool_spawn(&s->pool, &s->world, x, y);
        if (idx >= 0) {
            Creature *c = &s->pool.items[idx];
            c->energy = s->set.energy_start;
            c->heading = (uint8_t)rng_below(&s->rng, HEADING_COUNT);
            /* A placed creature has no parent: a random genome, and the
             * lineage pool_spawn gave it (its own id) starts a new line. */
            genome_random(&c->genome, &s->rng, &s->set);
            s->brain.on_birth(&s->brain, c->id, &c->genome);
            return idx;
        }
    }
    return -1;
}

/* How many founders this world gets: a share of its soil, at most the pool.
 * + 0.5f before converting to int rounds to the nearest whole creature. */
static int founder_count(const Sim *s)
{
    int soil = 0;
    for (int i = 0; i < s->world.w * s->world.h; i++) {
        if (s->world.terrain[i] == TERRAIN_SOIL) {
            soil++;
        }
    }
    int wanted = (int)(s->set.start_share * (float)soil + 0.5f);
    return wanted < s->pool.capacity ? wanted : s->pool.capacity;
}

static void sample(Sim *s)
{
    stats_sample(&s->stats, &s->world, &s->pool, s->tick, s->scratch);
}

bool sim_alloc(Sim *s, const Settings *set, int w, int h)
{
    /* Everything zero first: sim_free can then clean up after a failure
     * at any point below, since every free function accepts empty
     * parts. */
    *s = (Sim){0};
    s->set = *set; /* copies the whole struct, brain_name's characters too */
    if (!world_init(&s->world, w, h) ||
        !pool_init(&s->pool, set->pool_capacity)) {
        sim_free(s);
        return false;
    }
    s->turns = malloc((size_t)set->pool_capacity * sizeof(*s->turns));
    s->scratch = malloc((size_t)set->pool_capacity * sizeof(*s->scratch));
    if (s->turns == NULL || s->scratch == NULL) {
        sim_free(s);
        return false;
    }
    return true;
}

bool sim_init(Sim *s, const Settings *set, uint64_t seed, int w, int h)
{
    if (!sim_alloc(s, set, w, h)) {
        return false;
    }
    if (!sim_reseed(s, seed)) {
        sim_free(s);
        return false;
    }
    return true;
}

bool sim_resume(Sim *s)
{
    if (!brain_open(&s->brain, s->set.brain_name, &s->set,
                    s->seed ^ BRAIN_SEED_SALT)) {
        return false;
    }
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (c->alive) {
            s->brain.on_birth(&s->brain, c->id, &c->genome);
        }
    }
    stats_measure(&s->stats, &s->world, &s->pool, s->tick, s->scratch);
    return true;
}

bool sim_reseed(Sim *s, uint64_t seed)
{
    s->seed = seed;
    s->tick = 0;
    rng_seed(&s->rng, seed);
    world_clear(&s->world);
    pool_clear(&s->pool);
    /* The brain's random numbers start over too, so a reseeded Sim and a
     * fresh one make the same decisions. */
    brain_close(&s->brain);
    if (!brain_open(&s->brain, s->set.brain_name, &s->set,
                    seed ^ BRAIN_SEED_SALT)) {
        return false;
    }
    terrain_generate(&s->world, &s->rng, &s->set);
    plant_start(&s->world, &s->rng, &s->set);
    int founders = founder_count(s);
    for (int i = 0; i < founders; i++) {
        /* A failure means 64 random cells in a row were taken: the soil is
         * as good as full, so stop instead of searching further. */
        if (place_creature(s) < 0) {
            break;
        }
    }
    /* A first sample at tick 0, so the panel has numbers from the start,
     * and fresh histories for the new world. */
    stats_clear(&s->stats);
    sample(s);
    return true;
}

/* Fisher-Yates: walking down from the end, swap each entry with one at a
 * random position at or before it. Every order is equally likely. */
static void shuffle(Turn *turns, int n, Rng *r)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)rng_below(r, (uint32_t)(i + 1));
        /* A struct is copied by assignment, so the swap needs a
         * temporary like any other value. */
        Turn tmp = turns[i];
        turns[i] = turns[j];
        turns[j] = tmp;
    }
}

/* One creature's turn: ask the brain if the last answer has run out,
 * then do one tick of the current action. */
static void take_turn(Sim *s, int idx)
{
    Creature *c = &s->pool.items[idx];
    if (c->hold_left == 0) {
        Senses in;
        Action a;
        creature_sense(&s->world, &s->pool, idx, &s->set, &in);
        bool ready = s->brain.decide(&s->brain, c->id, &c->genome, &in, &a);
        /* The brain is not trusted: an unknown kind counts as resting,
         * like a brain that is not ready. */
        if (!ready || a.kind >= ACT_COUNT) {
            a = (Action){ACT_REST, 1};
        }
        c->current = a;
        c->hold_left = a.hold > 0 ? a.hold : 1;
    }
    c->hold_left--;
    int child = creature_act(&s->world, &s->pool, idx, c->current, &s->set,
                             &s->rng);
    if (child >= 0) {
        const Creature *born = &s->pool.items[child];
        s->brain.on_birth(&s->brain, born->id, &born->genome);
    }
}

bool sim_in_season(uint64_t tick, const Settings *set)
{
    /* Both settings are at least 0 and 1 by the table's check, so the casts
     * cannot turn a negative into a huge number, and % never divides by 0. */
    return tick % (uint64_t)set->season_every < (uint64_t)set->season_ticks;
}

/* Every creature alive at the start of the tick acts once, in an order
 * shuffled anew each tick, so no slot is always first to the food. */
static void creatures_act(Sim *s)
{
    int n = 0;
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            s->turns[n] = (Turn){i, s->pool.items[i].id};
            n++;
        }
    }
    shuffle(s->turns, n, &s->rng);
    pool_count_plant_eaters(&s->pool, &s->set);
    s->pool.in_season = sim_in_season(s->tick, &s->set);
    for (int k = 0; k < n; k++) {
        const Creature *c = &s->pool.items[s->turns[k].idx];
        /* Children born this tick are not in the list. A slot that holds
         * a newborn with another id is skipped. So is a creature whose
         * energy an attack took to 0 or below earlier in this tick: it is
         * dying, and the accounting removes it. */
        if (c->alive && c->id == s->turns[k].id && c->energy > 0.0f) {
            take_turn(s, s->turns[k].idx);
        }
    }
}

/* Upkeep for everyone, in slot order; those left without energy die. */
static void accounting(Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        Creature *c = &s->pool.items[i];
        if (c->alive && !creature_upkeep(c, &s->set)) {
            s->brain.on_death(&s->brain, c->id);
            pool_kill(&s->pool, &s->world, i, &s->set);
        }
    }
}

/* The floor: top the population up to min_creatures, so a long-running
 * world is never empty. Stops early if no free cell turns up. */
static void restock(Sim *s)
{
    while (s->pool.count < s->set.min_creatures) {
        if (place_creature(s) < 0) {
            break;
        }
    }
}

void sim_step(Sim *s)
{
    flora_step(&s->world, &s->rng, &s->set);
    carrion_step(&s->world, &s->set);
    scent_step(&s->world, &s->set);
    creatures_act(s);
    accounting(s);
    restock(s);
    s->tick++;
    if (s->set.stats_every > 0 && s->tick % (uint64_t)s->set.stats_every == 0) {
        sample(s);
    }
}

void sim_free(Sim *s)
{
    brain_close(&s->brain);
    free(s->turns);
    s->turns = NULL;
    free(s->scratch);
    s->scratch = NULL;
    pool_free(&s->pool);
    world_free(&s->world);
}

/*
 * FNV-1a, a simple hash over bytes: for each byte, mix it in with
 * exclusive-or, then multiply by a fixed prime (wrapping modulo 2^64).
 * data is "const void *", a pointer to bytes of any type; it is read
 * through an unsigned char pointer, the one type C allows to look at the
 * bytes of any object.
 */
static uint64_t fnv1a(uint64_t hash, const void *data, size_t len)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/* One living creature, field by field: Creature has padding, unused bytes the
 * compiler puts in so that floats start at a multiple of 4 (after alive, after
 * heading, inside Action between kind and hold, and more), so its raw bytes
 * are not safe to hash. */
static uint64_t hash_creature(uint64_t hash, const Creature *c)
{
    hash = fnv1a(hash, &c->id, sizeof(c->id));
    hash = fnv1a(hash, &c->lineage, sizeof(c->lineage));
    hash = fnv1a(hash, &c->x, sizeof(c->x));
    hash = fnv1a(hash, &c->y, sizeof(c->y));
    hash = fnv1a(hash, &c->heading, sizeof(c->heading));
    hash = fnv1a(hash, &c->energy, sizeof(c->energy));
    hash = fnv1a(hash, &c->age, sizeof(c->age));
    hash = fnv1a(hash, &c->current.kind, sizeof(c->current.kind));
    hash = fnv1a(hash, &c->current.hold, sizeof(c->current.hold));
    hash = fnv1a(hash, &c->hold_left, sizeof(c->hold_left));
    hash = fnv1a(hash, &c->move_points, sizeof(c->move_points));
    hash = fnv1a(hash, &c->mate_wish, sizeof(c->mate_wish));
    hash = fnv1a(hash, &c->level, sizeof(c->level));
    hash = fnv1a(hash, &c->eaten, sizeof(c->eaten));
    /* sizeof of an array that is a struct field is the whole array,
     * GENOME_LEN * sizeof(float): it has not decayed to a pointer here,
     * as an array passed to a function would. */
    hash = fnv1a(hash, c->genome.g, sizeof(c->genome.g));
    return hash;
}

/*
 * Hashes the state field by field, never the Sim struct's raw bytes:
 * the struct holds pointers (different addresses on every run) and may
 * hold padding, unused bytes the compiler inserts between fields to align
 * them, whose values are unspecified. The arrays hashed here have no
 * padding: uint8_t, uint16_t, int32_t, float and uint64_t elements sit
 * back to back. The brain's private state is not hashed: the Sim cannot
 * see inside it. Its effect shows up in what the creatures do.
 */
uint64_t sim_checksum(const Sim *s)
{
    const World *w = &s->world;
    size_t n = (size_t)w->w * (size_t)w->h;
    uint64_t hash = 14695981039346656037ULL; /* the FNV starting value */
    hash = fnv1a(hash, &s->tick, sizeof(s->tick));
    hash = fnv1a(hash, s->rng.s, sizeof(s->rng.s));
    hash = fnv1a(hash, w->terrain, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->fertility, n * sizeof(float));
    hash = fnv1a(hash, w->veg, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->veg_age, n * sizeof(uint16_t));
    hash = fnv1a(hash, w->veg_wither, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->veg_rule, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->carrion, n * sizeof(float));
    hash = fnv1a(hash, w->carrion_level, n * sizeof(float));
    hash = fnv1a(hash, w->scent, n * sizeof(float));
    hash = fnv1a(hash, w->occupant, n * sizeof(int32_t));
    hash = fnv1a(hash, &s->pool.count, sizeof(s->pool.count));
    hash = fnv1a(hash, &s->pool.next_id, sizeof(s->pool.next_id));
    hash = fnv1a(hash, &s->pool.births_refused,
                 sizeof(s->pool.births_refused));
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            hash = hash_creature(hash, &s->pool.items[i]);
        }
    }
    return hash;
}
