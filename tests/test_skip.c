#include "test.h"

#include <string.h> /* strstr */

#include "actions.h"
#include "settings.h"
#include "sim.h"
#include "skip.h"

/* A pretend clock for skip_step: every reading is 10 ms later than the last,
 * so a 25 ms budget ends a slice after a few ticks. "static" inside a
 * function keeps the variable between calls, like a global only this function
 * can see. */
static uint64_t fake_ms(void)
{
    static uint64_t now = 0;
    now += 10;
    return now;
}

static bool any_line_has(char lines[][SKIP_COLS], int n, const char *text)
{
    for (int i = 0; i < n; i++) {
        if (strstr(lines[i], text) != NULL) {
            return true;
        }
    }
    return false;
}

/* A skip run in slices is plain stepping: the same world comes out. */
static void test_skip_same_as_stepping(void)
{
    Settings set = settings_default();
    Sim a, b;
    CHECK(sim_init(&a, &set, 6, 64, 32));
    CHECK(sim_init(&b, &set, 6, 64, 32));
    Skip k = {0};
    CHECK(skip_start(&k, &a, 1));
    int slices = 0;
    while (skip_step(&k, &a, fake_ms, 25)) {
        slices++;
    }
    CHECK(slices > 10);
    while (b.tick < (uint64_t)set.ticks_per_year) {
        sim_step(&b);
    }
    CHECK(a.tick == b.tick);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    CHECK(k.n_years == 2); /* the start and the year's end */
    char lines[SKIP_LINES][SKIP_COLS];
    int n = skip_summary_lines(&k, &a, lines, SKIP_LINES);
    CHECK(n >= 3);
    CHECK(any_line_has(lines, n, "While you were away: 1 year "));
    skip_free(&k);
    sim_free(&a);
    sim_free(&b);
}

/* Stopped at once, there is still a summary, and it says it was stopped. */
static void test_cancel_at_once(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 6, 64, 32));
    Skip k = {0};
    CHECK(skip_start(&k, &s, 20));
    skip_cancel(&k);
    CHECK(!k.active && k.cancelled);
    CHECK(!skip_step(&k, &s, fake_ms, 25));
    CHECK(s.tick == 0);
    char lines[SKIP_LINES][SKIP_COLS];
    int n = skip_summary_lines(&k, &s, lines, SKIP_LINES);
    CHECK(any_line_has(lines, n, "Stopped after 0 of 20 years"));
    char progress[128];
    skip_progress_text(&k, &s, 0, progress, sizeof(progress));
    CHECK(strstr(progress, "of 20") != NULL);
    skip_free(&k);
    sim_free(&s);
}

/* A world that dies during the skip ends it early, and the summary says so. */
static void test_world_dies_during_skip(void)
{
    Settings set = settings_default();
    set.min_creatures = 0;
    set.energy_start = 1.0f; /* founders starve within days */
    Sim s;
    CHECK(sim_init(&s, &set, 6, 32, 16));
    Skip k = {0};
    CHECK(skip_start(&k, &s, 20));
    skip_finish(&k, &s);
    CHECK(sim_is_over(&s));
    CHECK(s.tick < 20u * (uint64_t)set.ticks_per_year);
    char lines[SKIP_LINES][SKIP_COLS];
    int n = skip_summary_lines(&k, &s, lines, SKIP_LINES);
    CHECK(any_line_has(lines, n, "The last creature died"));
    /* Nothing to skip in a dead world. */
    CHECK(!skip_start(&k, &s, 1));
    skip_free(&k);
    sim_free(&s);
}

/* A given trait is followed in the summary: carriers counted, or died out. */
static void test_summary_names_given_trait(void)
{
    Settings set = settings_default();
    set.mutation_rate = 0.0f;
    Sim s;
    CHECK(sim_init(&s, &set, 6, 64, 32));
    int given = 0;
    for (int i = 0; i < s.pool.capacity && given < 5; i++) {
        if (s.pool.items[i].alive) {
            given += actions_give_trait(&s, s.pool.items[i].id, GENE_ARMOUR,
                                        0.875f);
        }
    }
    CHECK(given == 5);
    Skip k = {0};
    CHECK(skip_start(&k, &s, 1));
    skip_finish(&k, &s);
    char lines[SKIP_LINES][SKIP_COLS];
    int n = skip_summary_lines(&k, &s, lines, SKIP_LINES);
    for (int i = 0; i < n; i++) {
        printf("  %s\n", lines[i]);
    }
    /* Told once, though five creatures got it. */
    int told = 0;
    for (int i = 0; i < n; i++) {
        told += strstr(lines[i], "armour you gave (0.88)") != NULL;
    }
    CHECK(told == 1);
    skip_free(&k);
    sim_free(&s);
}

int main(void)
{
    test_skip_same_as_stepping();
    test_cancel_at_once();
    test_world_dies_during_skip();
    test_summary_names_given_trait();
    TEST_SUMMARY();
}
