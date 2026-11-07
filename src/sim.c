#include "sim.h"

#include <stddef.h> /* size_t */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* memcpy */

#include "chronicle.h"
#include "climate.h"
#include "flora.h"
#include "terrain.h"
#include "years.h"

/* How many random cells to try when placing one creature before giving
 * up (the world may have little free soil left). */
#define PLACE_ATTEMPTS 64

/* Mixed into the seed for the brain, so the brain's random numbers are
 * not the same series as the world's. Any odd 64-bit constant would do;
 * this one is the fractional part of the golden ratio. */
#define BRAIN_SEED_SALT 0x9E3779B97F4A7C15ULL
/* The same idea for the climate's generator. */
#define CLIMATE_SEED_SALT 0xC2B2AE3D27D4EB4FULL

/* A starting plant's genes are all random, as a founder creature's are: most
 * will suit their place badly and die, and the ones that fit are the start of
 * selection. Separate statements, so the draws have a fixed order. */
static void random_plant(Plant *p, Rng *r, const Settings *s)
{
    p->energy = s->plant_start_energy;
    p->fruit = 0.0f;
    p->fruit_share = rng_unit(r);
    p->water_need = rng_unit(r);
    p->temp_pref = rng_unit(r);
    p->gut_time = rng_unit(r);
    p->size = 0.0f; /* founders start as seedlings too */
}

/* Random starting plants, in row order, with the same number of draws for every
 * soil cell, so that the count of draws does not depend on what the draws decide.
 * Each gets a random age up to the age of maturity: all born on one tick, they
 * would all wither together. */
static void plant_start(World *w, Rng *r, const Settings *s)
{
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->terrain[i] == TERRAIN_SOIL) {
            w->veg[i] = rng_unit(r) < s->veg_start_chance ? 1 : 0;
            random_plant(&w->plant[i], r, s);
            /* veg_mature_age is at most 65535 (the settings table), so it
             * fits; + 1u keeps rng_below from being asked for 0 values. */
            uint32_t age = rng_below(r, (uint32_t)s->veg_mature_age + 1u);
            w->veg_age[i] = w->veg[i] > 0 ? (uint16_t)age : 0;
        }
    }
}

/* Where this Sim records life stories, or NULL with the chronicle setting off. */
static Chronicle *recorder(Sim *s)
{
    return s->set.chronicle ? &s->chronicle : NULL;
}

/* The low 32 bits of the tick, the date events carry. */
static uint32_t story_date(const Sim *s)
{
    return (uint32_t)s->tick;
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
            c->health = creature_max_health(c, &s->set); /* needs the genes */
            creature_start_family(c);
            chronicle_note(recorder(s), c,
                           (LifeEvent){.tick = story_date(s), .kind = LIFE_BORN});
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

/* One creature against the records so far. Only strictly greater wins, so of
 * two equals the first keeps the record. Draws no random numbers. */
static void records_note(WorldRecords *r, const Creature *c, const Settings *set)
{
    if (c->age > r->oldest_age) {
        r->oldest_age = c->age;
        r->oldest_id = c->id;
    }
    float size = creature_size(c, set);
    if (size > r->largest_size) {
        r->largest_size = size;
        r->largest_id = c->id;
    }
}

/* A new world's records: its founders. */
static void records_start(Sim *s)
{
    s->records = (WorldRecords){.peak_population = s->pool.count};
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            records_note(&s->records, &s->pool.items[i], &s->set);
        }
    }
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
    if (s->turns == NULL || s->scratch == NULL ||
        !chronicle_init(&s->chronicle)) {
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

/* Tells the Pool the time of year, from the tick alone: creature_act and
 * creature_sense have no Sim to ask. Done before anyone acts, and again after a
 * load, since none of it is saved. */
static void tell_the_season(Sim *s)
{
    int per_year = s->set.ticks_per_year;
    s->pool.in_season = sim_in_season(s->tick, &s->set);
    /* A SEASON_ value is 0 to 3, so it fits a uint8_t; the cast says so. */
    s->pool.season = (uint8_t)years_season_of(s->tick, per_year);
    s->pool.year_phase = years_phase(s->tick, per_year);
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
    tell_the_season(s);
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
    chronicle_clear(&s->chronicle);
    /* The brain's random numbers start over too, so a reseeded Sim and a
     * fresh one make the same decisions. */
    brain_close(&s->brain);
    if (!brain_open(&s->brain, s->set.brain_name, &s->set,
                    seed ^ BRAIN_SEED_SALT)) {
        return false;
    }
    terrain_generate(&s->world, &s->rng, &s->set);
    /* The climate gets a generator of its own, as the brain does. Drawing from
     * s->rng would shift every number after it, so plants and creatures would
     * all come out different for a reason that has nothing to do with them. */
    Rng climate_rng;
    rng_seed(&climate_rng, seed ^ CLIMATE_SEED_SALT);
    climate_generate(&s->world, &climate_rng, &s->set);
    plant_start(&s->world, &s->rng, &s->set);
    int founders = founder_count(s);
    for (int i = 0; i < founders; i++) {
        /* A failure means 64 random cells in a row were taken: the soil is
         * as good as full, so stop instead of searching further. */
        if (place_creature(s) < 0) {
            break;
        }
    }
    records_start(s);
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

/* Stores what the brain chose to remember, held to -1..1. The brain is not
 * trusted: a NaN fails the first comparison and so becomes -1, which keeps it
 * out of the creature and out of every sense and save after this. */
static void keep_memory(Creature *c, const Action *a)
{
    for (int m = 0; m < MEMORY_VALUES; m++) {
        float v = a->memory[m];
        if (!(v > -1.0f)) {
            v = -1.0f;
        } else if (v > 1.0f) {
            v = 1.0f;
        }
        c->memory[m] = v;
    }
}

/* One creature's turn: ask the brain if the last answer has run out,
 * then do one tick of the current action. */
static void take_turn(Sim *s, int idx)
{
    Creature *c = &s->pool.items[idx];
    if (c->hold_left == 0) {
        Senses in;
        /* Every field not named is 0. An array cannot be assigned as a whole,
         * though a struct holding one can: hence memcpy for the memory here. */
        Action a = {.kind = ACT_REST, .hold = 1};
        memcpy(a.memory, c->memory, sizeof(a.memory));
        creature_sense(&s->world, &s->pool, idx, &s->set, &in);
        /* Here and not in creature_sense, which must draw no random numbers. */
        in.v[SENSE_RANDOM] = rng_unit(&s->rng);
        bool ready = s->brain.decide(&s->brain, c->id, &c->genome, &in, &a);
        /* The brain is not trusted: an unknown kind counts as resting,
         * like a brain that is not ready. */
        if (!ready || a.kind >= ACT_COUNT) {
            a.kind = ACT_REST;
            a.hold = 1;
        }
        keep_memory(c, &a);
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
    return years_season_of(tick, set->ticks_per_year) == SEASON_SPRING;
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
    tell_the_season(s);
    /* The conversion keeps the low 32 bits, which is defined for an unsigned
     * target, and all that comparing two recent ticks needs. */
    s->pool.now = (uint32_t)s->tick;
    for (int k = 0; k < n; k++) {
        const Creature *c = &s->pool.items[s->turns[k].idx];
        /* Children born this tick are not in the list. A slot that holds
         * a newborn with another id is skipped. So is a creature whose
         * health an attack took to 0 or below earlier in this tick: it is
         * dying, and the accounting removes it. */
        if (c->alive && c->id == s->turns[k].id && c->health > 0.0f) {
            take_turn(s, s->turns[k].idx);
        }
    }
}

/* Why a creature found dying in the accounting died. The last blow decides:
 * health already gone before upkeep means an attack took it, and health still
 * there after upkeep means only the age limit can have ended it. */
static DeathCause death_cause(bool hit_to_death, const Creature *c)
{
    if (hit_to_death) {
        return DEATH_WOUNDS;
    }
    return c->health > 0.0f ? DEATH_AGE : DEATH_HUNGER;
}

/* ---- life stories, from the accounting ---- */

/* The newest hit c took. Only for a creature that has taken one. */
static const Hit *last_hit(const Creature *c)
{
    return &c->recent_hits[(c->hits_taken - 1) % HITS_KEPT];
}

/* A living creature at the end of its tick: a fight it came through, a new region
 * it has settled in, and its size. Reads the world, changes only the story. */
static void story_living(Sim *s, Creature *c)
{
    Chronicle *ch = recorder(s);
    if (ch == NULL) {
        return;
    }
    uint32_t now = story_date(s);
    if (c->hits_taken > 0 && now - last_hit(c)->tick == FIGHT_GAP) {
        chronicle_note(ch, c, (LifeEvent){.tick = now, .kind = LIFE_SURVIVED,
                                          .other_id = last_hit(c)->by});
    }
    LifeTally *t = &c->tally;
    /* REGION_ values run from -1 to 4, so the conversion to int8_t keeps them. */
    int8_t region = (int8_t)climate_region(
        &s->world, world_idx(&s->world, c->x, c->y), &s->set);
    if (!(t->flags & TALLY_HOME_KNOWN)) {
        t->flags |= TALLY_HOME_KNOWN; /* where it first stands is home */
        t->home_region = region;
        t->region_here = region;
    }
    if (region != t->region_here) {
        t->region_here = region;
        t->region_ticks = 0;
    }
    if (t->region_ticks < UINT16_MAX) {
        t->region_ticks++;
    }
    if (region != t->home_region && region != REGION_NONE &&
        t->region_ticks >= REGION_SETTLE) {
        t->home_region = region;
        chronicle_note(ch, c, (LifeEvent){.tick = now, .kind = LIFE_MOVED,
                                          .value = region});
    }
    float size = creature_size(c, &s->set);
    if (size > t->max_size) {
        t->max_size = size;
    }
    if (size >= s->set.giant_size && !(t->flags & TALLY_GIANT)) {
        /* Tenths of a size, as a whole number; + 0.5f rounds to the nearest. */
        chronicle_note(ch, c, (LifeEvent){.tick = now, .kind = LIFE_GIANT,
                                          .value = (int32_t)(size * 10.0f + 0.5f)});
    }
}

/* A creature about to die: relatives standing near are told, then its death is
 * noted, while it still holds its slot. */
static void story_death(Sim *s, Creature *dead, DeathCause cause)
{
    Chronicle *ch = recorder(s);
    if (ch == NULL) {
        return;
    }
    uint32_t now = story_date(s);
    for (int dy = -KIN_NEAR; dy <= KIN_NEAR; dy++) {
        for (int dx = -KIN_NEAR; dx <= KIN_NEAR; dx++) {
            int slot = s->world.occupant[world_idx(&s->world, dead->x + dx,
                                                   dead->y + dy)];
            if (slot < 0 || &s->pool.items[slot] == dead) {
                continue;
            }
            Creature *near = &s->pool.items[slot];
            if (genome_kinship(&near->genome, &dead->genome) >= s->set.kin_sense) {
                chronicle_note(ch, near,
                               (LifeEvent){.tick = now, .kind = LIFE_KIN_LOST,
                                           .other_id = dead->id});
            }
        }
    }
    LifeEvent died = {.tick = now, .kind = LIFE_DIED, .value = (int32_t)cause};
    if (cause == DEATH_WOUNDS && dead->hits_taken > 0) {
        died.other_id = last_hit(dead)->by;
    }
    chronicle_note(ch, dead, died);
}

/* After the accounting: the largest and the oldest creature alive, each told the
 * first time in its life it holds that place. Ties go to the earlier slot. */
static void story_leaders(Sim *s)
{
    Chronicle *ch = recorder(s);
    if (ch == NULL || s->pool.count == 0) {
        return;
    }
    Creature *largest = NULL, *oldest = NULL;
    float largest_size = 0.0f;
    for (int i = 0; i < s->pool.capacity; i++) {
        Creature *c = &s->pool.items[i];
        if (!c->alive) {
            continue;
        }
        float size = creature_size(c, &s->set);
        if (largest == NULL || size > largest_size) {
            largest = c;
            largest_size = size;
        }
        if (oldest == NULL || c->age > oldest->age) {
            oldest = c;
        }
    }
    uint32_t now = story_date(s);
    if (!(largest->tally.flags & TALLY_WAS_LARGEST)) {
        chronicle_note(ch, largest,
                       (LifeEvent){.tick = now, .kind = LIFE_LARGEST,
                                   .value = (int32_t)(largest_size * 10.0f + 0.5f)});
    }
    if (!(oldest->tally.flags & TALLY_WAS_OLDEST)) {
        chronicle_note(ch, oldest, (LifeEvent){.tick = now, .kind = LIFE_OLDEST});
    }
}

/* Upkeep for everyone, in slot order; those left without health die. The
 * records are read here because this already visits every creature, after this
 * tick's births and before this tick's deaths, so a creature's last age counts. */
static void accounting(Sim *s)
{
    if (s->pool.count > s->records.peak_population) {
        s->records.peak_population = s->pool.count;
    }
    int season = years_season_of(s->tick, s->set.ticks_per_year);
    for (int i = 0; i < s->pool.capacity; i++) {
        Creature *c = &s->pool.items[i];
        if (!c->alive) {
            continue;
        }
        bool hit_to_death = c->health <= 0.0f; /* read before upkeep changes it */
        /* In slot order, seeing everyone still standing, the dying included, so a
         * creature's crowd does not depend on who happened to die first. */
        c->energy -= creature_crowd_cost(&s->world, c, &s->set);
        c->energy -= creature_region_cost(&s->world, c, &s->set);
        c->energy -= creature_heat_cost(&s->world, c, &s->set, season);
        bool lives = creature_upkeep(c, &s->set);
        records_note(&s->records, c, &s->set);
        if (lives) {
            story_living(s, c);
        } else {
            DeathCause cause = death_cause(hit_to_death, c);
            story_death(s, c, cause);
            s->brain.on_death(&s->brain, c->id);
            pool_kill(&s->pool, &s->world, i, &s->set, cause);
        }
    }
    story_leaders(s);
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

bool sim_is_over(const Sim *s)
{
    return s->pool.count == 0;
}

/* After a step: dates the end the first time the world is found empty. A world
 * nobody ever lived in has no end to date. A floor from a settings file can bring
 * a world back, and then it has not ended after all. True on the tick it ended. */
static bool note_ending(Sim *s)
{
    WorldRecords *r = &s->records;
    if (s->pool.count > 0) {
        r->ended_at = 0;
        return false;
    }
    if (r->peak_population == 0 || r->ended_at != 0) {
        return false;
    }
    r->ended_at = s->tick;
    return true;
}

void sim_step(Sim *s)
{
    /* Set each step rather than once: a pointer into this Sim, stored once,
     * would point at the old place if the Sim were ever copied elsewhere. */
    s->pool.chronicle = recorder(s);
    climate_step(&s->world, &s->set, s->tick);
    flora_step(&s->world, &s->rng, &s->set,
               years_season_of(s->tick, s->set.ticks_per_year));
    carrion_step(&s->world, &s->set);
    scent_step(&s->world, &s->set);
    creatures_act(s);
    accounting(s);
    restock(s);
    s->tick++;
    bool sample_due =
        s->set.stats_every > 0 && s->tick % (uint64_t)s->set.stats_every == 0;
    /* The programs stop stepping a dead world, so without this last sample the
     * panel would show the living population from up to stats_every ticks ago,
     * for ever. */
    if (note_ending(s) || sample_due) {
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
    chronicle_free(&s->chronicle);
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
    /* tally, events_noted and events are left out on purpose: they are the life
     * story, which no rule reads, and test_recording_leaves_fingerprints_equal
     * compares fingerprints with recording on and off. Hashing them would make
     * that test fail for a reason that says nothing about the world. */
    hash = fnv1a(hash, &c->id, sizeof(c->id));
    hash = fnv1a(hash, &c->lineage, sizeof(c->lineage));
    hash = fnv1a(hash, &c->family_hue, sizeof(c->family_hue));
    hash = fnv1a(hash, &c->x, sizeof(c->x));
    hash = fnv1a(hash, &c->y, sizeof(c->y));
    hash = fnv1a(hash, &c->heading, sizeof(c->heading));
    hash = fnv1a(hash, &c->energy, sizeof(c->energy));
    hash = fnv1a(hash, &c->health, sizeof(c->health));
    hash = fnv1a(hash, &c->grown, sizeof(c->grown));
    hash = fnv1a(hash, &c->hits_taken, sizeof(c->hits_taken));
    /* Hit is two uint32_t, so no padding: its raw bytes are safe to hash. Only
     * the used entries are always zero or real, and all of them are hashed. */
    hash = fnv1a(hash, c->recent_hits, sizeof(c->recent_hits));
    hash = fnv1a(hash, c->memory, sizeof(c->memory));
    hash = fnv1a(hash, &c->age, sizeof(c->age));
    hash = fnv1a(hash, &c->current.kind, sizeof(c->current.kind));
    hash = fnv1a(hash, &c->current.hold, sizeof(c->current.hold));
    hash = fnv1a(hash, &c->hold_left, sizeof(c->hold_left));
    hash = fnv1a(hash, &c->move_points, sizeof(c->move_points));
    hash = fnv1a(hash, &c->mate_wish, sizeof(c->mate_wish));
    hash = fnv1a(hash, &c->level, sizeof(c->level));
    hash = fnv1a(hash, &c->eaten, sizeof(c->eaten));
    hash = fnv1a(hash, &c->gut_seed, sizeof(c->gut_seed));
    hash = fnv1a(hash, &c->gut_ticks, sizeof(c->gut_ticks));
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
    /* Field by field, as for a creature: a struct's raw bytes may hold padding. */
    hash = fnv1a(hash, &s->records.peak_population,
                 sizeof(s->records.peak_population));
    hash = fnv1a(hash, &s->records.oldest_id, sizeof(s->records.oldest_id));
    hash = fnv1a(hash, &s->records.oldest_age, sizeof(s->records.oldest_age));
    hash = fnv1a(hash, &s->records.largest_id, sizeof(s->records.largest_id));
    hash = fnv1a(hash, &s->records.largest_size,
                 sizeof(s->records.largest_size));
    hash = fnv1a(hash, &s->records.ended_at, sizeof(s->records.ended_at));
    hash = fnv1a(hash, s->rng.s, sizeof(s->rng.s));
    hash = fnv1a(hash, w->terrain, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->fertility, n * sizeof(float));
    hash = fnv1a(hash, w->veg, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->veg_age, n * sizeof(uint16_t));
    hash = fnv1a(hash, w->veg_wither, n * sizeof(uint8_t));
    /* Plant is seven floats, so no padding: its raw bytes are safe to hash. */
    hash = fnv1a(hash, w->plant, n * sizeof(Plant));
    hash = fnv1a(hash, w->carrion, n * sizeof(float));
    hash = fnv1a(hash, w->carrion_level, n * sizeof(float));
    hash = fnv1a(hash, w->scent, n * sizeof(float));
    hash = fnv1a(hash, w->water, n * sizeof(float));
    hash = fnv1a(hash, w->temperature, n * sizeof(float));
    hash = fnv1a(hash, w->rain, n * sizeof(float));
    hash = fnv1a(hash, w->snow, n * sizeof(float));
    hash = fnv1a(hash, &w->rain_seed, sizeof(w->rain_seed));
    hash = fnv1a(hash, w->occupant, n * sizeof(int32_t));
    hash = fnv1a(hash, &s->pool.count, sizeof(s->pool.count));
    hash = fnv1a(hash, &s->pool.next_id, sizeof(s->pool.next_id));
    hash = fnv1a(hash, &s->pool.births_refused,
                 sizeof(s->pool.births_refused));
    hash = fnv1a(hash, s->pool.deaths, sizeof(s->pool.deaths));
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            hash = hash_creature(hash, &s->pool.items[i]);
        }
    }
    return hash;
}

/* One tally, field by field: LifeTally has padding after flags and the regions. */
static uint64_t hash_tally(uint64_t hash, const LifeTally *t)
{
    hash = fnv1a(hash, t->parents, sizeof(t->parents));
    hash = fnv1a(hash, &t->born, sizeof(t->born));
    hash = fnv1a(hash, &t->kills, sizeof(t->kills));
    hash = fnv1a(hash, &t->hunter_kills, sizeof(t->hunter_kills));
    hash = fnv1a(hash, &t->children, sizeof(t->children));
    hash = fnv1a(hash, &t->fights, sizeof(t->fights));
    hash = fnv1a(hash, &t->heavy_wounds, sizeof(t->heavy_wounds));
    hash = fnv1a(hash, &t->kin_lost, sizeof(t->kin_lost));
    hash = fnv1a(hash, &t->moves, sizeof(t->moves));
    hash = fnv1a(hash, &t->max_size, sizeof(t->max_size));
    hash = fnv1a(hash, &t->flags, sizeof(t->flags));
    hash = fnv1a(hash, &t->home_region, sizeof(t->home_region));
    hash = fnv1a(hash, &t->region_here, sizeof(t->region_here));
    hash = fnv1a(hash, &t->region_ticks, sizeof(t->region_ticks));
    return hash;
}

/* Events have 3 bytes of padding after kind, so field by field too. */
static uint64_t hash_events(uint64_t hash, const LifeEvent *e, int n)
{
    for (int i = 0; i < n; i++) {
        hash = fnv1a(hash, &e[i].tick, sizeof(e[i].tick));
        hash = fnv1a(hash, &e[i].kind, sizeof(e[i].kind));
        hash = fnv1a(hash, &e[i].other_id, sizeof(e[i].other_id));
        hash = fnv1a(hash, &e[i].value, sizeof(e[i].value));
    }
    return hash;
}

uint64_t sim_story_checksum(const Sim *s)
{
    uint64_t hash = 14695981039346656037ULL;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (c->alive) {
            hash = hash_tally(hash, &c->tally);
            hash = fnv1a(hash, &c->events_noted, sizeof(c->events_noted));
            hash = hash_events(hash, c->events, CREATURE_EVENTS);
        }
    }
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        const NotableLife *n = &s->chronicle.lives[i];
        hash = fnv1a(hash, &n->used, sizeof(n->used));
        hash = fnv1a(hash, &n->alive, sizeof(n->alive));
        hash = fnv1a(hash, &n->id, sizeof(n->id));
        hash = fnv1a(hash, &n->lineage, sizeof(n->lineage));
        hash = fnv1a(hash, &n->age, sizeof(n->age));
        hash = fnv1a(hash, &n->score, sizeof(n->score));
        hash = hash_tally(hash, &n->tally);
        hash = fnv1a(hash, &n->events_noted, sizeof(n->events_noted));
        hash = hash_events(hash, n->events, NOTABLE_EVENTS);
    }
    return hash;
}
