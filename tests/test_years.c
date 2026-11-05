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
    CHECK(formats_as(UINT64_MAX, 1, "18446744073709551615 y"));
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
    CHECK(formats_as(5, 0, "5 y"));
    CHECK(formats_as(5, -3, "5 y"));
    CHECK(years_whole(5, 0) == 5);
}

static void test_years_whole(void)
{
    CHECK(years_whole(0, 100) == 0);
    CHECK(years_whole(99, 100) == 0);
    CHECK(years_whole(100, 100) == 1);
    CHECK(years_whole(2785, 100) == 27);
    CHECK(years_whole(UINT64_MAX, 1) == INT_MAX);
}

/* The promise the setting makes: it is for display only. If the fingerprint
 * ever covered the settings or a rule read this one, these would part. */
static void test_ticks_per_year_cannot_change_a_world(void)
{
    Settings a_set = settings_default();
    Settings b_set = settings_default();
    CHECK(a_set.ticks_per_year == 100);
    b_set.ticks_per_year = 7;
    Sim a, b;
    CHECK(sim_init(&a, &a_set, 7, 64, 32));
    CHECK(sim_init(&b, &b_set, 7, 64, 32));
    for (int t = 0; t < 500; t++) {
        sim_step(&a);
        sim_step(&b);
    }
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    sim_free(&a);
    sim_free(&b);
}

static void test_years_season(void)
{
    /* The default year: 25 ticks each, spring first. */
    CHECK(strcmp(years_season(0, 100), "spring") == 0);
    CHECK(strcmp(years_season(24, 100), "spring") == 0);
    CHECK(strcmp(years_season(25, 100), "summer") == 0);
    CHECK(strcmp(years_season(50, 100), "autumn") == 0);
    CHECK(strcmp(years_season(75, 100), "winter") == 0);
    CHECK(strcmp(years_season(99, 100), "winter") == 0);
    CHECK(strcmp(years_season(100, 100), "spring") == 0); /* the next year */
    /* A year too short to split, a bad length, and the largest tick stay safe. */
    CHECK(strcmp(years_season(5, 1), "spring") == 0);
    CHECK(strcmp(years_season(5, 0), "spring") == 0);
    CHECK(strcmp(years_season(UINT64_MAX, 100), "spring") == 0); /* ends in ..15 */
}

/* With the default settings spring is the mating season: the name a viewer reads
 * should not contradict what the creatures are doing. */
static void test_spring_is_the_mating_season(void)
{
    Settings s = settings_default();
    for (uint64_t tick = 0; tick < 400; tick++) {
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
    test_ticks_per_year_cannot_change_a_world();
    TEST_SUMMARY();
}
