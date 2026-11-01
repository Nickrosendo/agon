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

/* Random starting plants: one draw per soil cell, in row order. */
static void plant_start(World *w, Rng *r, const Settings *s)
{
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->terrain[i] == TERRAIN_SOIL) {
            w->veg[i] = rng_unit(r) < s->veg_start_chance ? 1 : 0;
        }
    }
}

/* Places one creature on a random free soil cell with the starting
 * energy and a random heading, and tells the brain. Returns its slot, or
 * -1 if the pool is full or no free cell turned up. */
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
            s->brain.on_birth(&s->brain, c->id, NULL);
            return idx;
        }
    }
    return -1;
}

bool sim_init(Sim *s, const Settings *set, uint64_t seed, int w, int h)
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
    if (s->turns == NULL || !sim_reseed(s, seed)) {
        sim_free(s);
        return false;
    }
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
    for (int i = 0; i < s->set.start_creatures; i++) {
        place_creature(s);
    }
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
        bool ready = s->brain.decide(&s->brain, c->id, NULL, &in, &a);
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
        s->brain.on_birth(&s->brain, s->pool.items[child].id, NULL);
    }
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
    for (int k = 0; k < n; k++) {
        const Creature *c = &s->pool.items[s->turns[k].idx];
        /* Children born this tick are not in the list. A creature killed
         * earlier in this tick (possible from lesson 7) has alive false,
         * or its slot already holds a newborn with another id. */
        if (c->alive && c->id == s->turns[k].id) {
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
    creatures_act(s);
    accounting(s);
    restock(s);
    s->tick++;
}

void sim_free(Sim *s)
{
    brain_close(&s->brain);
    free(s->turns);
    s->turns = NULL;
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

/* One living creature, field by field: Creature has 9 bytes of padding
 * (3 after alive, 3 after heading, 1 inside Action between kind and
 * hold, 2 at the end), so its raw bytes are not safe to hash. */
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
    hash = fnv1a(hash, w->occupant, n * sizeof(int32_t));
    hash = fnv1a(hash, &s->pool.count, sizeof(s->pool.count));
    hash = fnv1a(hash, &s->pool.next_id, sizeof(s->pool.next_id));
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            hash = hash_creature(hash, &s->pool.items[i]);
        }
    }
    return hash;
}
