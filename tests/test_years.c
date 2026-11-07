#include "test.h"

#include <limits.h> /* INT_MAX */
#include <string.h> /* strcmp, strlen */

#include "settings.h"
#include "sim.h"
#include "years.h"

static bool formats_as(uint64_t ticks, int per_year, const char *expected)
{
    char out[32];
    years_format(out, sizeof(out), ticks, per_year);
    if (strcmp(out, expected) != 0) {
        printf("%llu ticks: got '%s', expected '%s'\n",
               (unsigned long long)ticks, out, expected);
        return false;
    }
    return true;
}

static void test_years_format(void)
{
    CHECK(formats_as(0, 100, "0 y"));
    CHECK(formats_as(9, 100, "0 y"));   /* under a tenth */
    CHECK(formats_as(99, 100, "0.9 y")); /* cut, not rounded up to 1 */
    CHECK(formats_as(100, 100, "1 y"));
    CHECK(formats_as(150, 100, "1.5 y"));
    CHECK(formats_as(UINT64_MAX, 100, "184467440737095516.1 y"));
    CHECK(formats_as(UINT64_MAX, 4, "4611686018427387903.7 y"));
    CHECK(formats_as(10, 7, "1.4 y"));
}

static void test_years_format_small_buffer(void)
{
    /* Cut, ended, and never past the end: the checker would report that. */
    char out[4];
    years_format(out, sizeof(out), 123456, 100);
    CHECK(strlen(out) == 3);
}

static void test_bad_year_length_is_safe(void)
{
    /* Read as the shortest year the calendar allows, 4 ticks, as the seasons
     * are: one length for the rules and the display. */
    CHECK(formats_as(5, 0, "1.2 y"));
    CHECK(formats_as(5, -3, "1.2 y"));
    CHECK(years_whole(5, 0) == 1);
}

static void test_years_whole(void)
{
    CHECK(years_whole(0, 100) == 0);
    CHECK(years_whole(99, 100) == 0);
    CHECK(years_whole(100, 100) == 1);
    CHECK(years_whole(2785, 100) == 27);
    CHECK(years_whole(UINT64_MAX, 1) == INT_MAX);
}

/* The year is a rule now: with 600 ticks, each season's 150 begin on the
 * right tick, and a new year starts again with spring. */
static void test_seasons_switch_at_the_right_ticks(void)
{
    CHECK(years_season_of(0, 600) == SEASON_SPRING);
    CHECK(years_season_of(149, 600) == SEASON_SPRING);
    CHECK(years_season_of(150, 600) == SEASON_SUMMER);
    CHECK(years_season_of(300, 600) == SEASON_AUTUMN);
    CHECK(years_season_of(450, 600) == SEASON_WINTER);
    CHECK(years_season_of(599, 600) == SEASON_WINTER);
    CHECK(years_season_of(600, 600) == SEASON_SPRING);
    CHECK(years_season_starts(600, 600, SEASON_SPRING));
    CHECK(!years_season_starts(601, 600, SEASON_SPRING));
    CHECK(years_season_starts(450, 600, SEASON_WINTER));
    CHECK(!years_season_starts(450, 600, SEASON_SPRING));
    CHECK(!years_season_starts(449, 600, SEASON_WINTER));
}

/* A year from a settings file may not split into four: every tick still has a
 * season, all four come round, and every year begins in spring. Lengths below 4
 * and nonsense ones are read as 4 and must not crash. */
static void test_odd_years_cover_every_tick(void)
{
    const int lengths[] = {4, 7, 601};
    for (int k = 0; k < 3; k++) {
        int length = lengths[k];
        bool seen[SEASON_COUNT] = {false};
        for (uint64_t t = 0; t < 2u * (uint64_t)length; t++) {
            int season = years_season_of(t, length);
            CHECK(season >= 0 && season < SEASON_COUNT);
            if (season >= 0 && season < SEASON_COUNT) {
                seen[season] = true;
            }
            if (t % (uint64_t)length == 0) {
                CHECK(season == SEASON_SPRING);
                CHECK(years_season_starts(t, length, SEASON_SPRING));
            }
        }
        for (int season = 0; season < SEASON_COUNT; season++) {
            CHECK(seen[season]);
        }
    }
    int a = years_season_of(5, 0), b = years_season_of(5, -3);
    CHECK(a >= 0 && a < SEASON_COUNT);
    CHECK(b >= 0 && b < SEASON_COUNT);
    CHECK(years_season_of(5, 1) == years_season_of(5, 4));
}

static void test_year_phase(void)
{
    CHECK(years_phase(0, 600) == 0.0f);
    CHECK(years_phase(300, 600) == 0.5f);
    CHECK(years_phase(599, 600) < 1.0f);
    CHECK(years_phase(600, 600) == 0.0f);
    CHECK(years_phase(UINT64_MAX, 600) < 1.0f);
    CHECK(years_phase(3, 0) >= 0.0f && years_phase(3, 0) < 1.0f);
}

static void test_years_season(void)
{
    /* A year of 100: 25 ticks each, spring first. */
    CHECK(strcmp(years_season(0, 100), "spring") == 0);
    CHECK(strcmp(years_season(24, 100), "spring") == 0);
    CHECK(strcmp(years_season(25, 100), "summer") == 0);
    CHECK(strcmp(years_season(50, 100), "autumn") == 0);
    CHECK(strcmp(years_season(75, 100), "winter") == 0);
    CHECK(strcmp(years_season(99, 100), "winter") == 0);
    CHECK(strcmp(years_season(100, 100), "spring") == 0); /* the next year */
    /* A year too short to split, a bad length, and the largest tick stay safe. */
    CHECK(strcmp(years_season(5, 1), "summer") == 0); /* a year of 4 */
    CHECK(strcmp(years_season(5, 0), "summer") == 0);
    CHECK(strcmp(years_season(UINT64_MAX, 100), "spring") == 0); /* ends in ..15 */
}

/* Spring is the mating season: the name a viewer reads cannot contradict what
 * the creatures are doing, since both come from years_season_of. */
static void test_spring_is_the_mating_season(void)
{
    Settings s = settings_default();
    for (uint64_t tick = 0; tick < 1300; tick++) {
        bool spring = strcmp(years_season(tick, s.ticks_per_year), "spring") == 0;
        CHECK(spring == sim_in_season(tick, &s));
    }
}

int main(void)
{
    test_years_season();
    test_spring_is_the_mating_season();
    test_years_format();
    test_years_format_small_buffer();
    test_bad_year_length_is_safe();
    test_years_whole();
    test_seasons_switch_at_the_right_ticks();
    test_odd_years_cover_every_tick();
    test_year_phase();
    TEST_SUMMARY();
}
