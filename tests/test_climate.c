#include "test.h"

#include "climate.h"
#include "rng.h"
#include "settings.h"
#include "sim.h"
#include "terrain.h"
#include "world.h"

/* All soil, so every cell's water can change. */
static void soil_world(World *w, int width, int height)
{
    CHECK(world_init(w, width, height));
    for (int i = 0; i < width * height; i++) {
        w->terrain[i] = TERRAIN_SOIL;
    }
}

/* rain_threshold's two ends: 0 rains everywhere, 1 nowhere at all. */
static void run_climate(World *w, const Settings *s, uint64_t from, int ticks)
{
    for (int t = 0; t < ticks; t++) {
        climate_step(w, s, from + (uint64_t)t);
    }
}

static void test_rain_is_seeded(void)
{
    Settings s = settings_default();
    World a, b, c;
    soil_world(&a, 64, 32);
    soil_world(&b, 64, 32);
    soil_world(&c, 64, 32);
    Rng r;
    rng_seed(&r, 1);
    climate_generate(&a, &r, &s);
    rng_seed(&r, 1);
    climate_generate(&b, &r, &s);
    rng_seed(&r, 2);
    climate_generate(&c, &r, &s);

    int same = 0, moved = 0;
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 64; x++) {
            float now = climate_rain_at(&a, &s, x, y, 500);
            /* Same seed, same rain; asking twice changes nothing. */
            CHECK(now == climate_rain_at(&b, &s, x, y, 500));
            CHECK(now == climate_rain_at(&a, &s, x, y, 500));
            same += now == climate_rain_at(&c, &s, x, y, 500);
            moved += now != climate_rain_at(&a, &s, x, y,
                                            500 + 3 * (uint64_t)s.rain_period);
        }
    }
    CHECK(same < 64 * 32);  /* another seed, another pattern */
    CHECK(moved > 64 * 32 / 4); /* and the pattern does change */
    world_free(&a);
    world_free(&b);
    world_free(&c);
}

/* The pattern changes slowly: no cell jumps between one tick and the next,
 * not even where one noise frame hands over to the next. */
static void test_rain_changes_gradually(void)
{
    Settings s = settings_default();
    World w;
    soil_world(&w, 64, 32);
    Rng r;
    rng_seed(&r, 3);
    climate_generate(&w, &r, &s);
    uint64_t boundary = 2 * (uint64_t)s.rain_period;
    for (int y = 0; y < 32; y += 3) {
        for (int x = 0; x < 64; x += 3) {
            float before = climate_rain_at(&w, &s, x, y, boundary - 1);
            float after = climate_rain_at(&w, &s, x, y, boundary);
            float step = after - before;
            CHECK(step < 0.01f && step > -0.01f);
        }
    }
    world_free(&w);
}

/* Fading between two noise frames must not shrink the rainy area half way. */
static void test_rain_area_holds_through_a_fade(void)
{
    Settings s = settings_default();
    World w;
    soil_world(&w, 160, 80);
    Rng r;
    rng_seed(&r, 5);
    climate_generate(&w, &r, &s);
    uint64_t starts[2] = {4 * (uint64_t)s.rain_period,
                          4 * (uint64_t)s.rain_period +
                              (uint64_t)s.rain_period / 2};
    float strongest[2] = {0.0f, 0.0f};
    for (int k = 0; k < 2; k++) {
        for (int y = 0; y < 80; y++) {
            for (int x = 0; x < 160; x++) {
                float v = climate_rain_at(&w, &s, x, y, starts[k]);
                strongest[k] = v > strongest[k] ? v : strongest[k];
            }
        }
    }
    /* A plain average of two noise fields would flatten its peaks by about
     * 0.7 at the middle; these stay of the same order. */
    CHECK(strongest[0] > 0.0f && strongest[1] > 0.5f * strongest[0]);
    world_free(&w);
}

static void test_soil_holds_then_loses_water(void)
{
    Settings s = settings_default();
    World w;
    soil_world(&w, 8, 8);
    s.rain_threshold = 0.0f;
    run_climate(&w, &s, 0, s.climate_every * 200);
    float wet = w.water[0];
    CHECK(wet > 0.5f);

    s.rain_threshold = 1.0f;
    /* One update later most of it is still there: soil holds water. */
    run_climate(&w, &s, 0, s.climate_every);
    CHECK(w.water[0] < wet);
    CHECK(w.water[0] > 0.9f * wet);
    world_free(&w);
}

static void test_rain_stops_ground_dries(void)
{
    Settings s = settings_default();
    World w;
    soil_world(&w, 8, 8);
    w.water[0] = 1.0f;
    w.temperature[0] = 0.5f;
    CHECK(climate_region(&w, 0, &s) == REGION_WET);
    s.rain_threshold = 1.0f;
    w.temperature[0] = 0.9f;
    run_climate(&w, &s, 0, 20000);
    CHECK(w.water[0] < s.region_dry_below);
    CHECK(climate_region(&w, 0, &s) == REGION_HOT_DRY);
    CHECK(w.water[0] >= 0.0f);
    world_free(&w);
}

static void test_rain_returns_ground_recovers(void)
{
    Settings s = settings_default();
    World w;
    soil_world(&w, 8, 8);
    w.temperature[0] = 0.9f;
    CHECK(climate_region(&w, 0, &s) == REGION_HOT_DRY);
    s.rain_threshold = 0.0f;
    run_climate(&w, &s, 0, 20000);
    CHECK(w.water[0] > s.region_wet_above);
    CHECK(climate_region(&w, 0, &s) == REGION_WET);
    CHECK(w.water[0] <= 1.0f);
    world_free(&w);
}

/* Only soil holds a changing amount; water and rock stay as they were. */
static void test_only_soil_changes(void)
{
    Settings s = settings_default();
    World w;
    soil_world(&w, 8, 8);
    w.terrain[1] = TERRAIN_WATER;
    w.terrain[2] = TERRAIN_SHALLOW;
    w.terrain[3] = TERRAIN_ROCK;
    s.rain_threshold = 0.0f;
    run_climate(&w, &s, 0, 2000);
    CHECK(w.water[0] > 0.0f);
    CHECK(w.water[1] == 0.0f);
    CHECK(w.water[2] == 0.0f);
    CHECK(w.water[3] == 0.0f);
    world_free(&w);
}

static void test_regions_cover_the_list(void)
{
    /* Every seed, not one lucky one: later rules lean on all five. */
    for (uint64_t seed = 1; seed <= 5; seed++) {
        Settings s = settings_default();
        Sim sim;
        CHECK(sim_init(&sim, &s, seed, 160, 80));
        int counts[REGION_COUNT] = {0};
        for (int i = 0; i < 160 * 80; i++) {
            int region = climate_region(&sim.world, i, &sim.set);
            if (region != REGION_NONE) {
                counts[region]++;
            }
        }
        for (int k = 0; k < REGION_COUNT; k++) {
            CHECK(counts[k] > 0);
        }
        sim_free(&sim);
    }
}

static void test_region_changes_with_water(void)
{
    Settings s = settings_default();
    World w;
    soil_world(&w, 8, 8);
    w.temperature[0] = 0.5f;
    w.water[0] = 0.5f;
    CHECK(climate_region(&w, 0, &s) == REGION_TEMPERATE);
    w.water[0] = s.region_wet_above + 0.01f;
    CHECK(climate_region(&w, 0, &s) == REGION_WET);
    w.temperature[0] = s.region_hot_above + 0.01f;
    w.water[0] = s.region_dry_below - 0.01f;
    CHECK(climate_region(&w, 0, &s) == REGION_HOT_DRY);
    w.temperature[0] = s.region_cold_below - 0.01f;
    CHECK(climate_region(&w, 0, &s) == REGION_COLD);
    w.terrain[0] = TERRAIN_SHALLOW;
    CHECK(climate_region(&w, 0, &s) == REGION_SHALLOW);
    w.terrain[0] = TERRAIN_WATER;
    CHECK(climate_region(&w, 0, &s) == REGION_NONE);
    w.terrain[0] = TERRAIN_ROCK;
    CHECK(climate_region(&w, 0, &s) == REGION_NONE);
    world_free(&w);
}

static void test_shallow_lies_at_the_shore(void)
{
    Settings s = settings_default();
    World w;
    CHECK(world_init(&w, 160, 80));
    Rng r;
    rng_seed(&r, 1);
    terrain_generate(&w, &r, &s);
    int shallow = 0, deep = 0;
    for (int i = 0; i < 160 * 80; i++) {
        shallow += w.terrain[i] == TERRAIN_SHALLOW;
        deep += w.terrain[i] == TERRAIN_WATER;
    }
    CHECK(shallow > 0);
    CHECK(deep > 0);

    /* Without the band, no shallows, and the soil is exactly the same. */
    World plain;
    CHECK(world_init(&plain, 160, 80));
    s.shallow_band = 0.0f;
    rng_seed(&r, 1);
    terrain_generate(&plain, &r, &s);
    for (int i = 0; i < 160 * 80; i++) {
        CHECK(plain.terrain[i] != TERRAIN_SHALLOW);
        CHECK((plain.terrain[i] == TERRAIN_SOIL) ==
              (w.terrain[i] == TERRAIN_SOIL));
    }
    world_free(&w);
    world_free(&plain);
}

/* Climate has its own random numbers and nothing reads it yet, so plants
 * and creatures must not notice how the rain falls. */
static void test_climate_changes_no_life_yet(void)
{
    Settings wet = settings_default();
    Settings dry = settings_default();
    dry.rain_threshold = 1.0f;
    Sim a, b;
    CHECK(sim_init(&a, &wet, 11, 64, 32));
    CHECK(sim_init(&b, &dry, 11, 64, 32));
    for (int t = 0; t < 300; t++) {
        sim_step(&a);
        sim_step(&b);
    }
    CHECK(a.pool.count == b.pool.count);
    for (int i = 0; i < 4; i++) {
        CHECK(a.rng.s[i] == b.rng.s[i]);
    }
    sim_free(&a);
    sim_free(&b);
}

/* The climate belongs to the fingerprint: a world that differs only in its
 * water must not count as the same world. */
static void test_fingerprint_covers_climate(void)
{
    Settings s = settings_default();
    Sim sim;
    CHECK(sim_init(&sim, &s, 4, 32, 16));
    uint64_t start = sim_checksum(&sim);
    float kept = sim.world.water[5];
    sim.world.water[5] = kept + 0.25f;
    CHECK(sim_checksum(&sim) != start);
    sim.world.water[5] = kept;
    kept = sim.world.temperature[5];
    sim.world.temperature[5] = kept + 0.25f;
    CHECK(sim_checksum(&sim) != start);
    sim.world.temperature[5] = kept;
    kept = sim.world.rain[5];
    sim.world.rain[5] = kept + 0.25f;
    CHECK(sim_checksum(&sim) != start);
    sim.world.rain[5] = kept;
    sim.world.rain_seed++;
    CHECK(sim_checksum(&sim) != start);
    sim.world.rain_seed--;
    CHECK(sim_checksum(&sim) == start);
    sim_free(&sim);
}

int main(void)
{
    test_rain_is_seeded();
    test_rain_changes_gradually();
    test_rain_area_holds_through_a_fade();
    test_soil_holds_then_loses_water();
    test_rain_stops_ground_dries();
    test_rain_returns_ground_recovers();
    test_only_soil_changes();
    test_regions_cover_the_list();
    test_region_changes_with_water();
    test_shallow_lies_at_the_shore();
    test_climate_changes_no_life_yet();
    test_fingerprint_covers_climate();
    TEST_SUMMARY();
}
