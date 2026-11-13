#include "test.h"

#include <stdlib.h> /* calloc, free */
#include <string.h> /* strstr, strncmp, strlen */

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

/* The line that starts with label, or NULL. */
static const char *chart_of(char lines[][SKIP_COLS], int n, const char *label)
{
    for (int i = 0; i < n; i++) {
        if (strncmp(lines[i], label, strlen(label)) == 0) {
            return lines[i];
        }
    }
    return NULL;
}

/* Blocks in a chart line: each is the three bytes E2 96 81..88. */
static int chart_columns(const char *line)
{
    int count = 0;
    for (const unsigned char *b = (const unsigned char *)line; *b != 0; b++) {
        if (b[0] == 0xE2 && b[1] == 0x96 && b[2] >= 0x81 && b[2] <= 0x88) {
            count++;
        }
    }
    return count;
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
    CHECK(k.n_samples == 5); /* the start and each season's end */
    char lines[SKIP_LINES][SKIP_COLS];
    int n = skip_summary_lines(&k, &a, lines, SKIP_LINES);
    CHECK(n >= 3);
    CHECK(any_line_has(lines, n, "While you were away: 1 year "));
    CHECK(any_line_has(lines, n, "Each column a season:"));
    /* A season a column, and the world now is the last sample: 5 blocks. */
    const char *chart = chart_of(lines, n, "creatures ");
    CHECK(chart != NULL);
    CHECK(chart != NULL && chart_columns(chart) == 5);
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

/* A long skip's chart is cut to SKIP_CHART_COLS columns that fit the line,
 * and an empty count draws as spaces, not blocks. The Skip is filled by hand:
 * 81 seasons is 20 years, too slow to run in a test. */
static void test_long_chart_fits(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 6, 32, 16));
    Skip k = {0};
    CHECK(skip_start(&k, &s, 20));
    skip_cancel(&k);
    free(k.samples);
    k.n_samples = k.samples_cap = 81;
    k.samples = calloc((size_t)k.n_samples, sizeof(*k.samples));
    CHECK(k.samples != NULL);
    for (int i = 0; k.samples != NULL && i < k.n_samples; i++) {
        k.samples[i].tick = (uint64_t)i * 150u;
        k.samples[i].population = 100 + 50 * i;
        k.samples[i].plant_eaters = 100 + 50 * i;
        k.samples[i].meat_eaters = 0;
    }
    /* The world now, at tick 0, is before the last sample: a new tick all
     * the same, so it is one more column, cut down like the rest. */
    char lines[SKIP_LINES][SKIP_COLS];
    int n = skip_summary_lines(&k, &s, lines, SKIP_LINES);
    CHECK(any_line_has(lines, n, "Each column about 0.5 years:"));
    const char *line = chart_of(lines, n, "creatures ");
    CHECK(line != NULL && chart_columns(line) == SKIP_CHART_COLS);
    CHECK(line != NULL && strstr(line, " to ") != NULL);
    /* A rising count: its first column is the lowest, the shortest block. */
    const char *first = line != NULL ? strstr(line, "\xE2\x96") : NULL;
    CHECK(first != NULL && (unsigned char)first[2] == 0x81);
    line = chart_of(lines, n, "meat-eaters ");
    CHECK(line != NULL && chart_columns(line) < SKIP_CHART_COLS);
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
    test_long_chart_fits();
    TEST_SUMMARY();
}
