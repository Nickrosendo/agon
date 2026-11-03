#include "test.h"

#include "settings.h"
#include "sim.h"

/*
 * The checksum of seed 7 at 64 by 32 after 500 steps with the default
 * settings. Pinned on purpose: if a change to the rules, the settings or
 * the order of random draws changes the simulation, this test fails, and
 * the new value must be put here deliberately, knowing why it changed.
 * Last changed with predation: traits cost upkeep and set speed and
 * sight, bites can attack, deaths leave carrion that rots, the carrion
 * layers and the new creature fields are hashed, and the start and the
 * floor went up to 150 creatures; then the digest floor; then separate
 * plant and meat digestion genes; then scent, and a bite that looks all
 * round and lets hunters hunt; then the hunter ray value, which adds
 * three network inputs and so 18 genes, changing every random genome;
 * then heritable plant rules: a third random draw per soil cell each
 * tick, and the rule layer hashed; then the costs of vigorous plant
 * rules (less food, earlier withering) and the plant-eater cap counted
 * by food-chain level, all three on by default; then slower, sparser,
 * more nourishing plants (sprout rate, capacity, bite energy); then
 * carrion no longer feeding the soil by default; then two parents in place
 * of splitting: six more senses and so 36 more genes, a crossing's draws, and
 * the wish to mate hashed; then limits off: no floor, no age limit, no plant
 * cap, no plants from nothing, and founders by share of the soil.
 */
#define SEED_7_AFTER_500 0x77b93ffe94956d91ULL

static void test_same_seed_same_checksum(void)
{
    Settings set = settings_default();
    Sim a, b;
    CHECK(sim_init(&a, &set, 7, 64, 32));
    CHECK(sim_init(&b, &set, 7, 64, 32));
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    sim_step(&a);
    sim_step(&b);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    for (int i = 1; i < 500; i++) {
        sim_step(&a);
        sim_step(&b);
    }
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    printf("seed 7 after 500 steps: 0x%016llx\n",
           (unsigned long long)sim_checksum(&a));
    /*
     * Only on Linux. The value depends on tanhf, powf, logf and cosf, and
     * Windows's maths library rounds some of their results differently, so its
     * worlds differ from the first such difference on. The checks above, two runs
     * agreeing with each other, are what must hold everywhere.
     */
#ifndef _WIN32
    CHECK(sim_checksum(&a) == SEED_7_AFTER_500);
#endif
    sim_free(&a);
    sim_free(&b);
}

static void test_other_seed_differs(void)
{
    Settings set = settings_default();
    Sim a, b;
    CHECK(sim_init(&a, &set, 7, 64, 32));
    CHECK(sim_init(&b, &set, 8, 64, 32));
    sim_step(&a);
    sim_step(&b);
    CHECK(sim_checksum(&a) != sim_checksum(&b));
    sim_free(&a);
    sim_free(&b);
}

static void test_reseed_matches_fresh_init(void)
{
    /* Reusing the memory for a new seed must give exactly the world a
     * fresh sim_init would. */
    Settings set = settings_default();
    Sim reused, fresh;
    CHECK(sim_init(&reused, &set, 1, 64, 32));
    for (int i = 0; i < 20; i++) {
        sim_step(&reused);
    }
    sim_reseed(&reused, 7);
    CHECK(sim_init(&fresh, &set, 7, 64, 32));
    CHECK(sim_checksum(&reused) == sim_checksum(&fresh));
    sim_free(&reused);
    sim_free(&fresh);
}

static void test_bad_size_fails(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(!sim_init(&s, &set, 7, 0, 32));
}

static int count_soil(const World *w)
{
    int soil = 0;
    for (int i = 0; i < w->w * w->h; i++) {
        soil += w->terrain[i] == TERRAIN_SOIL;
    }
    return soil;
}

static void kill_everyone(Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            pool_kill(&s->pool, &s->world, i, &s->set);
        }
    }
}

/* By default nobody is placed after tick 0: a next_id that stays put means no
 * creature appeared. */
static void test_no_creature_after_the_start(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 7, 64, 32));
    kill_everyone(&s);
    uint32_t before = s.pool.next_id;
    for (int t = 0; t < 100; t++) {
        sim_step(&s);
    }
    CHECK(s.pool.count == 0);
    CHECK(s.pool.next_id == before);
    sim_free(&s);
}

/* An empty world is an ordinary state: stepping, sampling and fingerprinting
 * it must all work, and it is still repeatable. */
static void test_empty_world_steps_safely(void)
{
    Settings set = settings_default();
    set.stats_every = 1;
    Sim a, b;
    CHECK(sim_init(&a, &set, 9, 64, 32));
    CHECK(sim_init(&b, &set, 9, 64, 32));
    kill_everyone(&a);
    kill_everyone(&b);
    for (int t = 0; t < 50; t++) {
        sim_step(&a);
        sim_step(&b);
    }
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    CHECK(a.stats.creatures == 0);
    sim_free(&a);
    sim_free(&b);
}

/* Founders follow the soil, so a small world is not swamped and a large one
 * is not empty. Within 1 %, or within a creature for a small count. */
static void check_founders(int width, int height)
{
    Settings set = settings_default();
    set.start_share = 0.05f;
    Sim s;
    CHECK(sim_init(&s, &set, 11, width, height));
    float wanted = set.start_share * (float)count_soil(&s.world);
    float margin = wanted * 0.01f + 1.0f;
    CHECK((float)s.pool.count >= wanted - margin);
    CHECK((float)s.pool.count <= wanted + margin);
    sim_free(&s);
}

static void test_founders_scale_with_soil(void)
{
    check_founders(40, 20);
    check_founders(256, 144);

    /* A share of 1 stops at the pool's size and comes back. */
    Settings set = settings_default();
    set.start_share = 1.0f;
    set.pool_capacity = 50;
    Sim s;
    CHECK(sim_init(&s, &set, 11, 40, 20));
    CHECK(s.pool.count == 50);
    sim_free(&s);

    /* A share of 1 with a pool larger than the soil: as much as can be
     * found, and it comes back too. */
    set.pool_capacity = 40 * 20;
    CHECK(sim_init(&s, &set, 11, 40, 20));
    CHECK(s.pool.count > 0);
    CHECK(s.pool.count <= count_soil(&s.world));
    sim_free(&s);

    /* A share of 0: nobody, which tests that place their own rely on. */
    set.start_share = 0.0f;
    CHECK(sim_init(&s, &set, 11, 40, 20));
    CHECK(s.pool.count == 0);
    sim_free(&s);
}

int main(void)
{
    test_same_seed_same_checksum();
    test_other_seed_differs();
    test_reseed_matches_fresh_init();
    test_bad_size_fails();
    test_no_creature_after_the_start();
    test_empty_world_steps_safely();
    test_founders_scale_with_soil();
    TEST_SUMMARY();
}
