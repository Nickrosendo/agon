#include "test.h"

#include <math.h>  /* fabsf */
#include <stdio.h> /* snprintf */

#include "brain_script.h"
#include "creature.h"
#include "settings.h"
#include "sim.h"
#include "world.h"

/* A world of all soil, fertility 1, no vegetation. */
static void make_world(World *w, int width, int height)
{
    CHECK(world_init(w, width, height));
    for (int i = 0; i < width * height; i++) {
        w->terrain[i] = TERRAIN_SOIL;
        w->fertility[i] = 1.0f;
    }
}

static void test_pool_slots_and_ids(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Pool p;
    CHECK(pool_init(&p, 3));
    int a = pool_spawn(&p, &w, 1, 1);
    int b = pool_spawn(&p, &w, 2, 1);
    int c = pool_spawn(&p, &w, 3, 1);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    CHECK(a != b && b != c && a != c);
    CHECK(pool_spawn(&p, &w, 4, 1) == -1); /* full */
    CHECK(p.count == 3);
    CHECK(w.occupant[world_idx(&w, 2, 1)] == b);

    uint32_t old_id = p.items[b].id;
    pool_kill(&p, &w, b, &s);
    CHECK(p.count == 2);
    CHECK(w.occupant[world_idx(&w, 2, 1)] == -1);
    pool_kill(&p, &w, b, &s); /* killing a free slot changes nothing */
    CHECK(p.count == 2);

    int d = pool_spawn(&p, &w, 5, 5);
    CHECK(d == b);                      /* the slot is reused */
    CHECK(p.items[d].id != old_id);     /* the id is not */
    CHECK(p.items[d].id == 3);
    pool_free(&p);
    world_free(&w);
}

static void test_spawn_needs_free_soil(void)
{
    World w;
    make_world(&w, 8, 8);
    Pool p;
    CHECK(pool_init(&p, 4));
    CHECK(pool_spawn(&p, &w, 3, 3) >= 0);
    CHECK(pool_spawn(&p, &w, 3, 3) == -1); /* occupied */
    w.terrain[world_idx(&w, 4, 4)] = TERRAIN_WATER;
    CHECK(pool_spawn(&p, &w, 4, 4) == -1);
    CHECK(p.count == 1);
    CHECK(p.next_id == 1); /* failed spawns use no id */
    pool_free(&p);
    world_free(&w);
}

static void test_move_wraps(void)
{
    World w;
    make_world(&w, 8, 6);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 4));
    int i = pool_spawn(&p, &w, 0, 0); /* heading north */
    p.items[i].energy = 10.0f;
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    CHECK(p.items[i].x == 0 && p.items[i].y == 5);
    CHECK(w.occupant[world_idx(&w, 0, 0)] == -1);
    CHECK(w.occupant[world_idx(&w, 0, 5)] == i);
    CHECK(fabsf(p.items[i].energy - (10.0f - s.move_cost)) < 1e-6f);
    pool_free(&p);
    world_free(&w);
}

static void test_move_blocked(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 4));
    int i = pool_spawn(&p, &w, 3, 3);
    p.items[i].energy = 10.0f;
    CHECK(pool_spawn(&p, &w, 3, 2) >= 0); /* someone to the north */
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    CHECK(p.items[i].x == 3 && p.items[i].y == 3);
    CHECK(fabsf(p.items[i].energy - (10.0f - s.move_cost)) < 1e-6f);

    /* Turn right to face east, where there is water, then rock. */
    creature_act(&w, &p, i, (Action){ACT_TURN_RIGHT, 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_EAST);
    w.terrain[world_idx(&w, 4, 3)] = TERRAIN_WATER;
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    w.terrain[world_idx(&w, 4, 3)] = TERRAIN_ROCK;
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    CHECK(p.items[i].x == 3 && p.items[i].y == 3);
    CHECK(fabsf(p.items[i].energy - (10.0f - 3 * s.move_cost)) < 1e-5f);
    pool_free(&p);
    world_free(&w);
}

static void test_turns(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = pool_spawn(&p, &w, 3, 3);
    creature_act(&w, &p, i, (Action){ACT_TURN_LEFT, 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_WEST); /* left of north */
    creature_act(&w, &p, i, (Action){ACT_TURN_RIGHT, 1}, &s, &r);
    creature_act(&w, &p, i, (Action){ACT_TURN_RIGHT, 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_EAST);
    pool_free(&p);
    world_free(&w);
}

static void test_bite(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = pool_spawn(&p, &w, 3, 3);
    p.items[i].energy = 10.0f;
    w.veg[world_idx(&w, 3, 2)] = 3; /* ahead, to the north */
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(w.veg[world_idx(&w, 3, 2)] == 2);
    CHECK(fabsf(p.items[i].energy - (10.0f + s.bite_veg_energy)) < 1e-6f);

    /* Biting bare ground gains nothing. */
    creature_act(&w, &p, i, (Action){ACT_TURN_RIGHT, 1}, &s, &r);
    float before = p.items[i].energy;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(p.items[i].energy == before);
    pool_free(&p);
    world_free(&w);
}

static void test_split(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 4));
    int i = pool_spawn(&p, &w, 3, 3);
    float before = s.split_threshold + 10.0f;
    p.items[i].energy = before;
    genome_random(&p.items[i].genome, &r, &s);
    int child = creature_act(&w, &p, i, (Action){ACT_SPLIT, 1}, &s, &r);
    CHECK(child >= 0 && child != i);
    CHECK(p.count == 2);
    /* Parent and child together hold what the parent held. Floats are
     * compared with a small tolerance, not ==: see the study doc. */
    float total = p.items[i].energy + p.items[child].energy;
    CHECK(fabsf(total - before) < 1e-4f);
    CHECK(p.items[child].lineage == p.items[i].lineage);
    /* The child's genome is a close but not exact copy. */
    const Genome *pg = &p.items[i].genome;
    const Genome *cg = &p.items[child].genome;
    CHECK(genome_kinship(pg, cg) > 0.9f);
    CHECK(genome_kinship(pg, cg) < 1.0f);
    /* The child is on one of the eight neighbouring cells. */
    int dx = p.items[child].x - 3;
    int dy = p.items[child].y - 3;
    CHECK(dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1 && (dx || dy));

    /* Below the threshold: nothing happens. */
    int count = p.count;
    CHECK(creature_act(&w, &p, i, (Action){ACT_SPLIT, 1}, &s, &r) == -1);
    CHECK(p.count == count);
    pool_free(&p);
    world_free(&w);
}

static void test_split_needs_room(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 16));
    int i = pool_spawn(&p, &w, 3, 3);
    p.items[i].energy = s.split_threshold * 2.0f;
    /* Water all around. */
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx || dy) {
                w.terrain[world_idx(&w, 3 + dx, 3 + dy)] = TERRAIN_WATER;
            }
        }
    }
    CHECK(creature_act(&w, &p, i, (Action){ACT_SPLIT, 1}, &s, &r) == -1);
    CHECK(p.count == 1);
    CHECK(p.items[i].energy == s.split_threshold * 2.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_sense(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = pool_spawn(&p, &w, 3, 3); /* heading north */
    p.items[i].energy = s.split_threshold / 2.0f;
    p.items[i].age = (uint32_t)s.sense_age_half;
    w.veg[world_idx(&w, 3, 2)] = VEG_MAX; /* ahead */
    w.veg[world_idx(&w, 2, 3)] = 2;       /* left: west */
    w.veg[world_idx(&w, 4, 3)] = 0;       /* right: east */
    Senses in;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(fabsf(in.v[SENSE_ENERGY] - 0.5f) < 1e-6f);
    CHECK(fabsf(in.v[SENSE_AGE] - 0.5f) < 1e-6f);
    CHECK(in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES + RAY_VEG] == 1.0f);
    CHECK(in.v[SENSE_RAYS + RAY_LEFT * RAY_VALUES + RAY_VEG] == 0.5f);
    CHECK(in.v[SENSE_RAYS + RAY_RIGHT * RAY_VALUES + RAY_VEG] == 0.0f);
    CHECK(in.v[SENSE_SIZE] == 0.0f); /* not filled in yet */
    pool_free(&p);
    world_free(&w);
}

/* A Sim using the scripted brain, with no creatures and no floor, whose
 * world is then turned into bare soil everywhere: whatever happens in the
 * test comes from the creatures it places itself. */
static void make_sim(Sim *sim, Settings *set)
{
    *set = settings_default();
    snprintf(set->brain_name, sizeof(set->brain_name), "script");
    set->start_creatures = 0;
    set->min_creatures = 0;
    set->veg_seed_chance = 0.0f;
    set->veg_start_chance = 0.0f;
    CHECK(sim_init(sim, set, 7, 16, 16));
    for (int i = 0; i < 16 * 16; i++) {
        sim->world.terrain[i] = TERRAIN_SOIL;
        sim->world.veg[i] = 0;
    }
}

static void test_starved_creature_dies(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int i = pool_spawn(&sim.pool, &sim.world, 4, 4);
    sim.pool.items[i].energy = set.upkeep_base / 2.0f;
    sim_step(&sim);
    CHECK(!sim.pool.items[i].alive);
    CHECK(sim.pool.count == 0);
    CHECK(sim.world.occupant[world_idx(&sim.world, 4, 4)] == -1);
    CHECK(brain_script_deaths(&sim.brain) == 1);
    sim_free(&sim);
}

static void test_old_age(void)
{
    Settings s = settings_default();
    s.max_age = 100;
    Creature c = {.alive = true, .energy = 1000.0f, .age = 98};
    CHECK(creature_upkeep(&c, &s));  /* turns 99 */
    CHECK(!creature_upkeep(&c, &s)); /* turns 100: dies, energy or not */
    CHECK(c.energy > 0.0f);

    s.max_age = 0; /* no limit */
    c.age = 1000000;
    CHECK(creature_upkeep(&c, &s));
}

static void test_hold_then_rest(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int i = pool_spawn(&sim.pool, &sim.world, 4, 4);
    sim.pool.items[i].energy = 100.0f;
    sim.pool.items[i].heading = HEADING_EAST;
    Action script[] = {{ACT_MOVE, 3}};
    brain_script_load(&sim.brain, script, 1);

    int xs[5];
    for (int t = 0; t < 5; t++) {
        sim_step(&sim);
        xs[t] = sim.pool.items[i].x;
    }
    /* Moves on three ticks, then rests. */
    CHECK(xs[0] == 5 && xs[1] == 6 && xs[2] == 7);
    CHECK(xs[3] == 7 && xs[4] == 7);
    CHECK(sim.pool.items[i].current.kind == ACT_REST);
    /* Asked on tick 1, then not again until the hold ran out: ticks 4
     * and 5 (each "not ready" rests one tick and asks again). */
    CHECK(brain_script_decisions(&sim.brain) == 3);
    sim_free(&sim);
}

static void test_floor_restocks(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.min_creatures = 5;
    sim_step(&sim);
    CHECK(sim.pool.count >= 5);
    /* Every creature killed: the next step brings the count back up. */
    for (int i = 0; i < sim.pool.capacity; i++) {
        if (sim.pool.items[i].alive) {
            pool_kill(&sim.pool, &sim.world, i, &sim.set);
        }
    }
    CHECK(sim.pool.count == 0);
    sim_step(&sim);
    CHECK(sim.pool.count >= 5);
    CHECK(brain_script_births(&sim.brain) >= 10);
    sim_free(&sim);
}

static void test_full_pool_runs(void)
{
    /* The random brain splits whenever it can; with a pool of 8 started
     * full, every birth must be refused without harm. The checkers in the
     * test build report any out-of-bounds access. */
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "random");
    set.pool_capacity = 8;
    set.start_creatures = 8;
    set.min_creatures = 8;
    set.split_threshold = 1.0f; /* everyone can split at once */
    Sim sim;
    CHECK(sim_init(&sim, &set, 3, 32, 32));
    CHECK(sim.pool.count == 8);
    int over = 0;
    for (int t = 0; t < 1000; t++) {
        sim_step(&sim);
        if (sim.pool.count > 8) {
            over++;
        }
    }
    CHECK(over == 0);
    sim_free(&sim);
}

static void test_bad_brain_name(void)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "nope");
    Sim sim;
    CHECK(!sim_init(&sim, &set, 1, 16, 16));
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_pool_slots_and_ids();
    test_spawn_needs_free_soil();
    test_move_wraps();
    test_move_blocked();
    test_turns();
    test_bite();
    test_split();
    test_split_needs_room();
    test_sense();
    test_starved_creature_dies();
    test_old_age();
    test_hold_then_rest();
    test_floor_restocks();
    test_full_pool_runs();
    test_bad_brain_name();
    TEST_SUMMARY();
}
