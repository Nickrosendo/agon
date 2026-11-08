#include "test.h"

#include "settings.h"
#include "sim.h"
#include "years.h"

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
 * cap, no plants from nothing, and founders by share of the soil; then
 * starting plants with mixed ages and a rule that can sprout from one plant;
 * then the mating season: one more sense, so 6 more genes, and more mating;
 * then ageing: one more gene, and upkeep that rises with age; then the world's
 * records, added to the fingerprint: the world itself is unchanged, shown by
 * the pinned pictures in test_scene.c staying as they were; then the sense of
 * being blocked: one more sense, so 6 more genes, changing every random genome;
 * then the nearest plant along each ray: three more senses, so 18 more genes;
 * then fights: one more action, so 7 more genes, a bite that only eats, and
 * health and hits in the fingerprint; then armour and weapon: two more genes,
 * changing every random genome; then growth: one more gene, bites that scale with
 * size, and a size that goes on rising; then memory and drives: eight more senses
 * and three more outputs, and one more random draw per decision; then life costs
 * less, more so at rest, close relatives are not struck, and families can split;
 * then crowding costs energy, and one more gene, territory, drives attacks; then
 * attacks spare similar colours; then the climate: rain, soil water, temperature
 * and shallows in the fingerprint, with plants and creatures unchanged (shown by
 * balance_report giving the same bytes before and after); then living plants:
 * each with its energy, fruit and genes, and two digestion genes for plants;
 * then a blocked walk turns aside: one more random draw for each; then regions'
 * edges: two more genes, and shallows, colour, size and legs matter; then wet and
 * dry times: rain follows a yearly-like cycle, with a shorter pattern; then soil
 * that dries twice as fast; then the balance against starving and lost leaf
 * plants: roots kept, cheaper fruit, leaner leaves, dearer crowding, more founders;
 * then the year became 600 ticks with spring as the mating season, and two year
 * senses re-rolled every brain; then snow and drier summers: less rain from
 * tick 150 on changes the plants and so everyone; then plants follow the
 * seasons: more in spring, fruit trees resting from tick 300, bare leaves;
 * then leaf trees grow and shade: Plant gains its size, and creatures out of shade
 * pay the summer heat; then deep sleep: a new action re-rolled every brain;
 * then settings tuned for the seasons: bigger bites of leaves, fruit and meat
 * and slower ageing change what every creature has; then a sleep lasts at least
 * 30 ticks, so brains that chose short sleeps sleep longer; then naps and
 * hibernation: a nap lasts 10 ticks at rest's cost, and only a well-fed winter
 * sleep is cheap and lasts until spring; then the plants' birth capacity, on by
 * default: births slow as the living near half the plants on the map, which
 * draws a random number at each birth; then a partner may stand two steps
 * away, so pairs that stood just apart now have children.
 */
#define SEED_7_AFTER_500 0xc6978583783f1fedULL

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
            pool_kill(&s->pool, &s->world, i, &s->set, DEATH_WOUNDS);
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

/* Plants on their own must keep going, now that nothing seeds them from
 * nowhere: with the old rule they died out on 3 of 5 seeds within 2,000 ticks,
 * leaving no food for anyone. Also, starting plants must not all be one age,
 * or they would all wither together. */
/* Every plant gone: the world keeps stepping, nothing grows from nothing, and
 * the animals follow to game over. */
static void test_all_plants_dead_is_allowed(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 1, 64, 32));
    int n = s.world.w * s.world.h;
    for (int i = 0; i < n; i++) {
        s.world.veg[i] = 0;
    }
    int regrown = 0;
    for (int t = 0; t < 60000 && !sim_is_over(&s); t++) {
        sim_step(&s);
        /* Seeds in guts cannot exist either: nobody could have eaten fruit. */
        for (int i = 0; i < n; i++) {
            regrown += s.world.veg[i] > 0;
        }
    }
    CHECK(regrown == 0);
    CHECK(sim_is_over(&s));
    sim_free(&s);
}

static void test_plants_alone_survive(void)
{
    Settings set = settings_default();
    set.start_share = 0.0f;
    for (uint64_t seed = 1; seed <= 5; seed++) {
        Sim s;
        CHECK(sim_init(&s, &set, seed, 128, 72));
        int young = 0;
        int old = 0;
        for (int i = 0; i < s.world.w * s.world.h; i++) {
            if (s.world.veg[i] > 0) {
                young += s.world.veg_age[i] < set.veg_mature_age / 2;
                old += s.world.veg_age[i] >= set.veg_mature_age / 2;
            }
        }
        CHECK(young > 0 && old > 0);
        for (int t = 0; t < 2000; t++) {
            sim_step(&s);
        }
        int planted = 0;
        for (int i = 0; i < s.world.w * s.world.h; i++) {
            planted += s.world.veg[i] > 0;
        }
        /* Well above nothing: 5 to 11 % of the soil at this moment, with the
         * ground drying as it does. */
        CHECK(planted * 33 > count_soil(&s.world));
        sim_free(&s);
    }
}

/* The mating season is spring: the first quarter of every year. */
static void test_season_calendar(void)
{
    Settings set = settings_default();
    CHECK(set.ticks_per_year == 600);
    CHECK(sim_in_season(0, &set));
    CHECK(sim_in_season(149, &set));
    CHECK(!sim_in_season(150, &set));
    CHECK(!sim_in_season(599, &set));
    CHECK(sim_in_season(600, &set));
    CHECK(sim_in_season(600ULL * 1000000000ULL, &set));
    set.ticks_per_year = 8;
    CHECK(sim_in_season(1, &set));
    CHECK(!sim_in_season(2, &set));
}

/* The Sim keeps the pool's flag to the calendar, and a saved world's future
 * does not depend on it being saved: it comes from the tick. */
static void test_sim_follows_the_season(void)
{
    Settings set = settings_default();
    set.ticks_per_year = 12;
    Sim s;
    CHECK(sim_init(&s, &set, 4, 64, 32));
    for (int t = 0; t < 40; t++) {
        bool expected = sim_in_season(s.tick, &s.set);
        int season = years_season_of(s.tick, set.ticks_per_year);
        sim_step(&s);
        CHECK(s.pool.in_season == expected);
        CHECK(s.pool.season == season);
    }
    sim_free(&s);
}

/* A world with only the creatures the test places, on all-soil ground. */
static void make_empty_world(Sim *s)
{
    Settings set = settings_default();
    set.start_share = 0.0f;
    set.veg_start_chance = 0.0f;
    CHECK(sim_init(s, &set, 7, 64, 32));
    for (int i = 0; i < 64 * 32; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
    }
}

/* A creature with plenty of energy, far enough from the others not to mate. */
static Creature *place_one(Sim *s, int x, int y, uint32_t age, float size_gene)
{
    int slot = pool_spawn(&s->pool, &s->world, x, y);
    CHECK(slot >= 0);
    Creature *c = &s->pool.items[slot];
    c->energy = 50.0f;
    c->age = age;
    c->genome.g[GENE_SIZE] = size_gene;
    return c;
}

static void test_records_start_from_the_founders(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 7, 64, 32));
    CHECK(s.pool.count > 0);
    CHECK(s.records.peak_population == s.pool.count);
    CHECK(s.records.largest_size > 0.0f);
    CHECK(s.records.ended_at == 0);
    /* A new world forgets the old one's records. */
    s.records.oldest_age = 123456;
    s.records.peak_population = 1000000;
    CHECK(sim_reseed(&s, 8));
    CHECK(s.records.oldest_age == 0);
    CHECK(s.records.peak_population == s.pool.count);
    sim_free(&s);
}

static void test_records_follow_the_world(void)
{
    Sim s;
    make_empty_world(&s);
    Creature *old = place_one(&s, 2, 2, 500, 0.2f);
    Creature *big = place_one(&s, 30, 2, 10, 1.0f);
    place_one(&s, 2, 20, 20, 0.5f);
    uint32_t old_id = old->id;
    uint32_t big_id = big->id;
    float big_size = creature_size(big, &s.set);
    sim_step(&s);
    CHECK(s.records.peak_population >= 3);
    CHECK(s.records.oldest_id == old_id);
    CHECK(s.records.oldest_age == 501); /* one tick older */
    CHECK(s.records.largest_id == big_id);
    CHECK(s.records.largest_size == big_size);

    /* Records stay when the ones who set them die. */
    int peak = s.records.peak_population;
    kill_everyone(&s);
    sim_step(&s);
    CHECK(s.records.peak_population == peak);
    CHECK(s.records.oldest_id == old_id && s.records.oldest_age == 501);
    CHECK(s.records.largest_id == big_id);
    sim_free(&s);
}

/* Reading the records and asking whether the world is over must leave it as it
 * would have been. */
static void test_records_do_not_change_the_world(void)
{
    Settings set = settings_default();
    Sim plain, read;
    CHECK(sim_init(&plain, &set, 3, 64, 32));
    CHECK(sim_init(&read, &set, 3, 64, 32));
    int total = 0;
    for (int t = 0; t < 300; t++) {
        sim_step(&plain);
        sim_step(&read);
        total += read.records.peak_population + (sim_is_over(&read) ? 1 : 0);
    }
    CHECK(total > 0);
    CHECK(sim_checksum(&plain) == sim_checksum(&read));
    sim_free(&plain);
    sim_free(&read);
}

/* The last creature starves: over from that tick on, and the date stays. */
static void test_is_over(void)
{
    Sim s;
    make_empty_world(&s);
    CHECK(sim_is_over(&s));        /* nobody has ever lived here */
    CHECK(s.records.ended_at == 0);
    sim_step(&s);
    CHECK(s.records.ended_at == 0); /* nobody died, so nothing ended */

    Creature *c = place_one(&s, 5, 5, 0, 0.5f);
    CHECK(!sim_is_over(&s));
    sim_step(&s);
    CHECK(!sim_is_over(&s));
    CHECK(s.records.ended_at == 0);

    c->energy = 0.000001f;
    c->health = s.set.starve_damage / 2.0f;
    uint64_t last_tick = s.tick;
    sim_step(&s);
    CHECK(sim_is_over(&s));
    CHECK(s.records.ended_at == last_tick + 1);
    CHECK(s.records.ended_at == s.tick);
    /* The sample is taken at the end too, so the panel does not go on showing
     * a living population that is several ticks out of date. */
    CHECK(s.stats.creatures == 0);
    CHECK(s.stats.tick == s.tick);

    uint64_t ended = s.records.ended_at;
    sim_step(&s);
    sim_step(&s);
    CHECK(s.records.ended_at == ended);
    sim_free(&s);
}

/* A settings file can still ask for a floor. A world it brings back is not
 * over, so it must not carry a date of death. */
static void test_floor_brings_a_world_back(void)
{
    Settings set = settings_default();
    set.min_creatures = 5;
    Sim s;
    CHECK(sim_init(&s, &set, 7, 64, 32));
    kill_everyone(&s);
    sim_step(&s);
    CHECK(s.pool.count >= 5);
    CHECK(!sim_is_over(&s));
    CHECK(s.records.ended_at == 0);
    sim_free(&s);
}

int main(void)
{
    test_records_start_from_the_founders();
    test_records_follow_the_world();
    test_records_do_not_change_the_world();
    test_is_over();
    test_floor_brings_a_world_back();
    test_same_seed_same_checksum();
    test_other_seed_differs();
    test_reseed_matches_fresh_init();
    test_bad_size_fails();
    test_no_creature_after_the_start();
    test_empty_world_steps_safely();
    test_founders_scale_with_soil();
    test_plants_alone_survive();
    test_all_plants_dead_is_allowed();
    test_season_calendar();
    test_sim_follows_the_season();
    TEST_SUMMARY();
}
