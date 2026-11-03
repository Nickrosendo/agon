#include "test.h"

#include <stdio.h> /* snprintf */

#include "brain_script.h"
#include "inspect.h"
#include "settings.h"
#include "sim.h"

/* A 16 by 16 Sim with soil everywhere and nobody in it. */
static void make_empty_sim(Sim *sim)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "script");
    set.start_share = 0.0f;
    set.min_creatures = 0;
    set.veg_start_chance = 0.0f;
    CHECK(sim_init(sim, &set, 7, 16, 16));
    for (int i = 0; i < 16 * 16; i++) {
        sim->world.terrain[i] = TERRAIN_SOIL;
    }
}

static void test_start_in_the_middle_when_empty(void)
{
    Sim sim;
    make_empty_sim(&sim);
    Inspect in = {0};
    inspect_start(&in, &sim, 10, 6);
    CHECK(in.on);
    CHECK(in.x == 5 && in.y == 3);
    CHECK(!in.following);
    CHECK(inspect_creature(&in, &sim) == NULL);
    sim_free(&sim);
}

static void test_start_on_the_oldest_in_view(void)
{
    Sim sim;
    make_empty_sim(&sim);
    int young = pool_spawn(&sim.pool, &sim.world, 2, 2);
    int old = pool_spawn(&sim.pool, &sim.world, 4, 3);
    int older_out_of_view = pool_spawn(&sim.pool, &sim.world, 12, 12);
    CHECK(young >= 0 && old >= 0 && older_out_of_view >= 0);
    sim.pool.items[young].age = 5;
    sim.pool.items[old].age = 50;
    sim.pool.items[older_out_of_view].age = 500;
    Inspect in = {0};
    inspect_start(&in, &sim, 8, 8);
    CHECK(in.x == 4 && in.y == 3);
    CHECK(in.following);
    CHECK(inspect_creature(&in, &sim) == &sim.pool.items[old]);
    sim_free(&sim);
}

static void test_move_stops_at_the_edges(void)
{
    Sim sim;
    make_empty_sim(&sim);
    Inspect in = {0};
    inspect_start(&in, &sim, 10, 6);
    inspect_move(&in, &sim, -100, 0, 10, 6);
    CHECK(in.x == 0);
    inspect_move(&in, &sim, 0, 100, 10, 6);
    CHECK(in.y == 5);
    inspect_move(&in, &sim, 1, -1, 10, 6);
    CHECK(in.x == 1 && in.y == 4);
    sim_free(&sim);
}

static void test_follows_until_death(void)
{
    Sim sim;
    make_empty_sim(&sim);
    int idx = pool_spawn(&sim.pool, &sim.world, 6, 6);
    CHECK(idx >= 0);
    Creature *c = &sim.pool.items[idx];
    c->energy = 50.0f;
    c->genome.g[GENE_SPEED] = 1.0f; /* a move point every tick */
    Inspect in = {0};
    inspect_start(&in, &sim, 16, 16);
    inspect_move(&in, &sim, 0, 0, 16, 16); /* stays: on the creature */
    CHECK(in.following);

    /* It moves north one cell; the cursor goes with it. */
    creature_act(&sim.world, &sim.pool, idx, (Action){.kind = ACT_MOVE, .hold = 1}, &sim.set,
                 &sim.rng);
    CHECK(c->y == 5);
    inspect_update(&in, &sim);
    CHECK(in.x == 6 && in.y == 5);

    /* It dies: the cursor stays where it was, following nobody. */
    pool_kill(&sim.pool, &sim.world, idx, &sim.set, DEATH_WOUNDS);
    inspect_update(&in, &sim);
    CHECK(!in.following);
    CHECK(in.x == 6 && in.y == 5);
    CHECK(inspect_creature(&in, &sim) == NULL);

    /* Moving off an empty cell onto an empty cell follows nobody. */
    inspect_move(&in, &sim, 1, 0, 16, 16);
    CHECK(!in.following);
    sim_free(&sim);
}

static Creature *add(Sim *sim, int x, int y)
{
    int slot = pool_spawn(&sim->pool, &sim->world, x, y);
    CHECK(slot >= 0);
    return &sim->pool.items[slot];
}

static void test_picks_for_the_panel(void)
{
    Sim sim;
    make_empty_sim(&sim);
    CHECK(inspect_highest_level(&sim) == -1);
    CHECK(inspect_oldest(&sim) == -1);
    CHECK(inspect_largest_of_family(&sim, 5) == -1);

    Creature *a = add(&sim, 1, 1);
    Creature *b = add(&sim, 5, 5);
    Creature *c = add(&sim, 9, 9);
    a->level = 1.0f;
    b->level = 3.7f;
    c->level = 2.0f;
    a->age = 900;
    b->age = 10;
    c->age = 20;
    a->lineage = b->lineage = 5;
    c->lineage = 6;
    a->genome.g[GENE_SIZE] = 0.9f;
    b->genome.g[GENE_SIZE] = 0.1f;
    c->genome.g[GENE_SIZE] = 1.0f;

    CHECK(&sim.pool.items[inspect_highest_level(&sim)] == b);
    CHECK(&sim.pool.items[inspect_oldest(&sim)] == a);
    CHECK(&sim.pool.items[inspect_largest_of_family(&sim, 5)] == a);
    CHECK(&sim.pool.items[inspect_largest_of_family(&sim, 6)] == c);
    CHECK(inspect_largest_of_family(&sim, 7) == -1);

    /* A dead one's old numbers do not count. */
    pool_kill(&sim.pool, &sim.world, inspect_highest_level(&sim), &sim.set, DEATH_WOUNDS);
    CHECK(&sim.pool.items[inspect_highest_level(&sim)] == c);
    sim_free(&sim);
}

/* A creature anywhere can be followed, and the camera's cursor goes with it. */
static void test_follow_a_slot(void)
{
    Sim sim;
    make_empty_sim(&sim);
    Creature *a = add(&sim, 3, 12);
    Inspect in = {0};
    inspect_follow(&in, &sim, (int)(a - sim.pool.items));
    CHECK(in.on && in.following);
    CHECK(in.x == 3 && in.y == 12);
    CHECK(inspect_creature(&in, &sim) == a);
    sim_free(&sim);
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_start_in_the_middle_when_empty();
    test_start_on_the_oldest_in_view();
    test_move_stops_at_the_edges();
    test_follows_until_death();
    test_picks_for_the_panel();
    test_follow_a_slot();
    TEST_SUMMARY();
}
