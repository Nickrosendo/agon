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
 * carrion no longer feeding the soil by default.
 */
#define SEED_7_AFTER_500 0x5d2acadc4d55dbc1ULL

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
    CHECK(sim_checksum(&a) == SEED_7_AFTER_500);
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

int main(void)
{
    test_same_seed_same_checksum();
    test_other_seed_differs();
    test_reseed_matches_fresh_init();
    test_bad_size_fails();
    TEST_SUMMARY();
}
