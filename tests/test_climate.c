#include "test.h"

#include <stdlib.h> /* malloc, free */
#include <string.h> /* memcmp, memcpy */

#include "climate.h"
#include "rng.h"
#include "settings.h"
#include "sim.h"
#include "terrain.h"
#include "world.h"
#include "years.h"

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

/* The share of soil-or-not cells it is raining on at this tick. */
static float rain_share(const World *w, const Settings *s, uint64_t tick)
{
    int raining = 0;
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            raining += climate_rain_at(w, s, x, y, tick) > 0.0f;
        }
    }
    return (float)raining / (float)(w->w * w->h);
}

/* Wet and dry times take turns, and each wet time rains somewhere new. */
static void test_rain_has_wet_and_dry_times(void)
{
    Settings s = settings_default();
    /* The cycle alone: 8,000 falls in a summer and 9,000 in a spring, and
     * summer's own rise is tested on its own below. */
    s.summer_rain_rise = 0.0f;
    World w;
    CHECK(world_init(&w, 128, 64));
    w.rain_seed = 99;
    uint64_t cycle = (uint64_t)s.rain_cycle;
    /* Each moment's pattern differs, so judged over several worlds. */
    float wet = 0.0f, dry = 0.0f;
    for (uint64_t seed = 1; seed <= 8; seed++) {
        w.rain_seed = seed * 7777;
        wet += rain_share(&w, &s, 4 * cycle) / 8.0f;
        dry += rain_share(&w, &s, 4 * cycle + cycle / 2) / 8.0f;
    }
    CHECK(wet > 0.55f);
    CHECK(dry < 0.06f);
    /* The pattern fades on through the dry time: two wet times in a row share
     * no more than chance would give. */
    int both = 0, either = 0;
    for (int y = 0; y < w.h; y++) {
        for (int x = 0; x < w.w; x++) {
            bool now = climate_rain_at(&w, &s, x, y, 4 * cycle) > 0.0f;
            bool next = climate_rain_at(&w, &s, x, y, 5 * cycle) > 0.0f;
            both += now && next;
            either += now || next;
        }
    }
    CHECK((float)both / (float)either < 0.85f);
    /* No swing, no wet and dry times. */
    s.rain_swing = 0.0f;
    float flat_wet = 0.0f, flat_dry = 0.0f;
    for (uint64_t seed = 1; seed <= 8; seed++) {
        w.rain_seed = seed * 7777;
        flat_wet += rain_share(&w, &s, 4 * cycle) / 8.0f;
        flat_dry += rain_share(&w, &s, 4 * cycle + cycle / 2) / 8.0f;
    }
    CHECK(flat_wet - flat_dry < 0.15f && flat_dry - flat_wet < 0.15f);
    /* A threshold of 1 still means no rain at all, at any time of the cycle. */
    s.rain_swing = 0.3f;
    s.rain_threshold = 1.0f;
    CHECK(rain_share(&w, &s, 4 * cycle) == 0.0f);
    world_free(&w);
}

/* Winter of the default 600-tick year: ticks 450 to 599. 475 is an update tick
 * (climate_every 25 divides it). */
#define WINTER_UPDATE 475u

static bool close_to(float a, float b)
{
    float d = a - b;
    return d < 1e-6f && d > -1e-6f;
}

/* In winter the rain piles up as snow at the full rate, the soil gets none of
 * it, and the soil still dries as on any other day. */
static void test_winter_rain_becomes_snow(void)
{
    Settings s = settings_default();
    s.rain_threshold = 0.0f; /* rain on every cell */
    World w;
    soil_world(&w, 16, 8);
    w.rain_seed = 5;
    for (int i = 0; i < 16 * 8; i++) {
        w.water[i] = 0.5f;
    }
    climate_step(&w, &s, WINTER_UPDATE);
    float ticks = (float)s.climate_every;
    int snowed = 0;
    for (int i = 0; i < 16 * 8; i++) {
        float expected = s.rain_gain * w.rain[i] * ticks;
        expected = expected > 1.0f ? 1.0f : expected;
        CHECK(close_to(w.snow[i], expected));
        CHECK(close_to(w.water[i], 0.5f - s.water_loss * 0.5f * ticks));
        snowed += w.snow[i] > 0.0f;
    }
    CHECK(snowed > 0);
    /* The same update in spring soaks in and leaves no snow. */
    World spring;
    soil_world(&spring, 16, 8);
    spring.rain_seed = 5;
    spring.water[0] = 0.5f;
    climate_step(&spring, &s, 25);
    CHECK(spring.snow[0] == 0.0f);
    CHECK(spring.water[0] > 0.5f - s.water_loss * 0.5f * ticks);
    world_free(&w);
    world_free(&spring);
}

/* All the snow melts on the first tick of spring, update tick or not, and the
 * soil never holds more than full. */
static void test_spring_melts_all_snow(void)
{
    Settings s = settings_default();
    s.rain_threshold = 1.0f; /* no rain, so only the melt changes the water */
    s.climate_every = 7;     /* 600 is not a multiple of 7: no update then */
    World w;
    soil_world(&w, 4, 4);
    w.snow[0] = 0.4f;
    w.water[0] = 0.5f;
    w.snow[1] = 0.4f;
    w.water[1] = 0.8f;
    climate_step(&w, &s, 599); /* the last tick of winter: nothing melts */
    CHECK(w.snow[0] == 0.4f && w.water[0] == 0.5f);
    climate_step(&w, &s, 600);
    CHECK(close_to(w.water[0], 0.9f));
    CHECK(w.snow[0] == 0.0f);
    CHECK(w.water[1] == 1.0f);
    CHECK(w.snow[1] == 0.0f);
    /* An odd year: spring starts at 601, and 601 melts. */
    s.ticks_per_year = 601;
    w.snow[2] = 0.3f;
    climate_step(&w, &s, 600);
    CHECK(w.snow[2] == 0.3f);
    climate_step(&w, &s, 601);
    CHECK(w.snow[2] == 0.0f && close_to(w.water[2], 0.3f));
    /* Tick 0 is a spring start too, and melts nothing: a new world. */
    w.snow[3] = 0.2f;
    climate_step(&w, &s, 0);
    CHECK(w.snow[3] == 0.2f);
    world_free(&w);
}

/* Summer raises the threshold, so less of the map gets rain than at the same
 * moment of the wet and dry cycle without the rise; spring is untouched. A
 * threshold pushed past 1 means no rain, and nothing divides by zero. */
static void test_summer_rains_on_less_ground(void)
{
    Settings s = settings_default();
    World w;
    CHECK(world_init(&w, 128, 64));
    w.rain_seed = 4242;
    uint64_t summer = 4 * (uint64_t)s.rain_cycle; /* 8,000: 200 into a year */
    uint64_t spring = summer - 200;               /* 7,800: a year's start */
    CHECK(years_season_of(summer, s.ticks_per_year) == SEASON_SUMMER);
    CHECK(years_season_of(spring, s.ticks_per_year) == SEASON_SPRING);
    float with_rise = rain_share(&w, &s, summer);
    float spring_with = rain_share(&w, &s, spring);
    s.summer_rain_rise = 0.0f;
    float without_rise = rain_share(&w, &s, summer);
    float spring_without = rain_share(&w, &s, spring);
    CHECK(with_rise < without_rise);
    CHECK(with_rise > 0.0f); /* drier, not dry */
    CHECK(spring_with == spring_without);

    s.rain_swing = 0.0f;
    s.rain_threshold = 0.95f;
    s.summer_rain_rise = 0.1f; /* 1.05 in summer */
    CHECK(rain_share(&w, &s, summer) == 0.0f);
    CHECK(rain_share(&w, &s, spring) > 0.0f);
    world_free(&w);
}

/* Twenty winters of rain everywhere, at the fastest gain allowed: snow never
 * leaves 0..1, and none lies on water or rock. */
static void test_snow_stays_in_range(void)
{
    Settings s = settings_default();
    s.rain_threshold = 0.0f;
    s.rain_gain = 1.0f;
    World w;
    soil_world(&w, 8, 8);
    w.terrain[1] = TERRAIN_WATER;
    w.terrain[2] = TERRAIN_ROCK;
    w.rain_seed = 77;
    float most = 0.0f;
    for (uint64_t t = 0; t < 20 * 600; t++) {
        climate_step(&w, &s, t);
        for (int i = 0; i < 64; i++) {
            CHECK(w.snow[i] >= 0.0f && w.snow[i] <= 1.0f);
            CHECK(w.water[i] >= 0.0f && w.water[i] <= 1.0f);
            most = w.snow[i] > most ? w.snow[i] : most;
        }
        CHECK(w.snow[1] == 0.0f && w.snow[2] == 0.0f);
    }
    CHECK(most == 1.0f); /* it did reach the cap */
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

/* Climate has its own random numbers, so however the rain falls it takes none
 * from life's generator. Rain does reach life, through plants that need water
 * and regions that ask things of bodies, so two whole worlds would part
 * quickly: this runs the climate alone. */
static void test_climate_takes_no_random_numbers(void)
{
    Settings s = settings_default();
    Sim sim;
    CHECK(sim_init(&sim, &s, 11, 64, 32));
    Rng before = sim.rng;
    size_t bytes = (size_t)(sim.world.w * sim.world.h) * sizeof(float);
    float *water = malloc(bytes);
    CHECK(water != NULL);
    memcpy(water, sim.world.water, bytes);
    for (uint64_t t = 0; t < 500; t++) {
        climate_step(&sim.world, &sim.set, t);
    }
    /* A struct cannot be compared with ==, and its bytes are four uint64_t. */
    for (int i = 0; i < 4; i++) {
        CHECK(sim.rng.s[i] == before.s[i]);
    }
    CHECK(memcmp(water, sim.world.water, bytes) != 0);
    free(water);
    sim_free(&sim);
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
    kept = sim.world.snow[5];
    sim.world.snow[5] = kept + 0.25f;
    CHECK(sim_checksum(&sim) != start);
    sim.world.snow[5] = kept;
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
    test_winter_rain_becomes_snow();
    test_spring_melts_all_snow();
    test_summer_rains_on_less_ground();
    test_snow_stays_in_range();
    test_soil_holds_then_loses_water();
    test_rain_stops_ground_dries();
    test_rain_returns_ground_recovers();
    test_only_soil_changes();
    test_regions_cover_the_list();
    test_region_changes_with_water();
    test_shallow_lies_at_the_shore();
    test_rain_has_wet_and_dry_times();
    test_climate_takes_no_random_numbers();
    test_fingerprint_covers_climate();
    TEST_SUMMARY();
}
