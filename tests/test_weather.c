#include "test.h"

#include <string.h> /* memcmp */

#include "actions.h"
#include "save.h"
#include "settings.h"
#include "sim.h"
#include "weather.h"

#define SAVE_FILE "build/test_weather.save"

static void run_to(Sim *s, uint64_t tick)
{
    while (s->tick < tick) {
        sim_step(s);
    }
}

/* The food the plants hold: stored energy and ripe fruit, over every planted
 * cell. It answers to the weather within a season, where the number of planted
 * cells moves only slowly, by sprouting. */
static double plant_food(const Sim *s)
{
    double total = 0.0;
    for (int i = 0; i < s->world.w * s->world.h; i++) {
        if (s->world.veg[i] != 0) {
            total += s->world.plant[i].energy + s->world.plant[i].fruit;
        }
    }
    return total;
}

/* No event: the settings come back exactly as they went in. */
static void test_none_is_identity(void)
{
    Settings base = settings_default();
    Weather none = {0};
    Settings got = weather_settings(&base, &none, 10);
    CHECK(memcmp(&got, &base, sizeof(base)) == 0);
    /* An event that has ended counts as none. */
    Weather over = {.kind = WEATHER_DROUGHT, .until = 10};
    got = weather_settings(&base, &over, 10);
    CHECK(memcmp(&got, &base, sizeof(base)) == 0);
    Weather on = {.kind = WEATHER_DROUGHT, .until = 11};
    got = weather_settings(&base, &on, 10);
    CHECK(got.plant_gain < base.plant_gain);
}

/* An event lasts one season from the tick it is applied, then is forgotten,
 * and the player's settings are back in the Sim after every tick. */
static void test_event_lasts_a_season(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 5, 64, 32));
    run_to(&s, 30);
    CHECK(actions_weather(&s, WEATHER_HARD_WINTER));
    CHECK(!actions_weather(&s, WEATHER_NONE));
    CHECK(!actions_weather(&s, WEATHER_COUNT));
    sim_step(&s);
    uint64_t season = (uint64_t)set.ticks_per_year / 4u;
    CHECK(s.weather.kind == WEATHER_HARD_WINTER);
    CHECK(s.weather.until == 30 + season);
    CHECK(s.set.cold_upkeep == set.cold_upkeep); /* put back after the tick */
    run_to(&s, 30 + season + 1);
    CHECK(s.weather.kind == WEATHER_NONE);
    sim_free(&s);
}

/* Saved half way through an event, the loaded world finishes it on the same
 * tick and stays the same world. */
static void test_save_mid_event(void)
{
    Settings set = settings_default();
    Sim a;
    CHECK(sim_init(&a, &set, 5, 64, 32));
    CHECK(actions_weather(&a, WEATHER_DROUGHT));
    run_to(&a, 100);
    char err[256] = "";
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    Sim b;
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(b.weather.kind == WEATHER_DROUGHT && b.weather.until == a.weather.until);
    uint64_t end = (uint64_t)set.ticks_per_year;
    run_to(&a, end);
    run_to(&b, end);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    sim_free(&a);
    sim_free(&b);
    remove(SAVE_FILE);
}

/* A drought leaves less plant food and a warm year more, within its season.
 * Started in summer, so the event's season is a growing one. */
static void test_events_have_effects(void)
{
    Settings set = settings_default();
    uint64_t start = (uint64_t)set.ticks_per_year / 4u; /* summer */
    uint64_t end = start + (uint64_t)set.ticks_per_year / 4u;
    double food[WEATHER_COUNT];
    for (int kind = 0; kind < WEATHER_COUNT; kind++) {
        Sim s;
        CHECK(sim_init(&s, &set, 5, 128, 64));
        run_to(&s, start);
        if (kind != WEATHER_NONE) {
            CHECK(actions_weather(&s, kind));
        }
        run_to(&s, end);
        food[kind] = plant_food(&s);
        sim_free(&s);
    }
    printf("plant food at the season's end: clear %.0f, drought %.0f, "
           "hard winter %.0f, warm %.0f\n", food[WEATHER_NONE],
           food[WEATHER_DROUGHT], food[WEATHER_HARD_WINTER],
           food[WEATHER_WARM_YEAR]);
    CHECK(food[WEATHER_DROUGHT] < 0.8 * food[WEATHER_NONE]);
    CHECK(food[WEATHER_WARM_YEAR] > food[WEATHER_NONE]);
}

/* A hard winter makes cold costlier: creatures spend more energy, so fewer of
 * them are alive a season later than with clear weather. */
static void test_hard_winter_costs_more(void)
{
    Settings set = settings_default();
    uint64_t end = (uint64_t)set.ticks_per_year / 2u;
    int alive[2];
    for (int k = 0; k < 2; k++) {
        Sim s;
        CHECK(sim_init(&s, &set, 5, 128, 64));
        if (k == 1) {
            CHECK(actions_weather(&s, WEATHER_HARD_WINTER));
        }
        run_to(&s, end);
        alive[k] = s.pool.count;
        sim_free(&s);
    }
    printf("alive half a year on: clear %d, hard winter %d\n", alive[0], alive[1]);
    CHECK(alive[1] < alive[0]);
}

int main(void)
{
    test_none_is_identity();
    test_event_lasts_a_season();
    test_save_mid_event();
    test_events_have_effects();
    test_hard_winter_costs_more();
    TEST_SUMMARY();
}
