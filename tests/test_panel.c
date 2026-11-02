#include "test.h"

#include <math.h>   /* fabsf */
#include <string.h> /* strcmp */

#include "brain_script.h"
#include "panel.h"
#include "settings.h"
#include "sim.h"

static int formats_as(long n, const char *expected)
{
    char out[32];
    panel_format_count(out, sizeof(out), n);
    return strcmp(out, expected) == 0;
}

static void test_format_count(void)
{
    CHECK(formats_as(0, "0"));
    CHECK(formats_as(7, "7"));
    CHECK(formats_as(999, "999"));
    CHECK(formats_as(1000, "1,000"));
    CHECK(formats_as(12345, "12,345"));
    CHECK(formats_as(1234567, "1,234,567"));
    CHECK(formats_as(-1234, "-1,234"));
}

static void test_format_count_small_buffer(void)
{
    /* Cut short, but always ended with '\0' and never written past the
     * end: the address checker would report a write past out[3]. */
    char out[4];
    panel_format_count(out, sizeof(out), 12345);
    CHECK(strcmp(out, "12,") == 0);
    char one[1];
    panel_format_count(one, sizeof(one), 5);
    CHECK(one[0] == '\0');
}

/* The eight block heights as UTF-8, lowest first: U+2581 to U+2588, each
 * three bytes, of which only the last differs. */
static const char *const BLOCK[8] = {
    "\xE2\x96\x81", "\xE2\x96\x82", "\xE2\x96\x83", "\xE2\x96\x84",
    "\xE2\x96\x85", "\xE2\x96\x86", "\xE2\x96\x87", "\xE2\x96\x88",
};

static void test_sparkline_empty(void)
{
    History h = {0};
    char out[128];
    panel_sparkline(out, sizeof(out), &h, 16);
    CHECK(strcmp(out, "                ") == 0); /* 16 spaces */
}

static void test_sparkline_newest_on_the_right(void)
{
    /* 16 columns over 64 samples: 4 samples per column. Four samples
     * fill only the rightmost column; the tallest block, since it is the
     * largest value shown. */
    History h = {0};
    for (int i = 0; i < 4; i++) {
        history_add(&h, 8);
    }
    char out[128];
    panel_sparkline(out, sizeof(out), &h, 16);
    char expected[128];
    snprintf(expected, sizeof(expected), "%15s%s", "", BLOCK[7]);
    CHECK(strcmp(out, expected) == 0);
}

static void test_sparkline_ramp(void)
{
    /* Values 0 .. 63: column c averages 4c .. 4c + 3, which is 4c + 1 in
     * whole numbers. The lowest column (1 of 61) gets the lowest block,
     * the highest the tallest. 16 characters of 3 bytes each. */
    History h = {0};
    for (int i = 0; i < HISTORY; i++) {
        history_add(&h, i);
    }
    char out[128];
    panel_sparkline(out, sizeof(out), &h, 16);
    CHECK(strlen(out) == 16 * 3);
    CHECK(strncmp(out, BLOCK[0], 3) == 0);
    CHECK(strcmp(out + 15 * 3, BLOCK[7]) == 0);
}

static void test_sparkline_zero_is_blank(void)
{
    /* A column whose samples are all 0 (nobody of that kind alive) is a
     * space, not the lowest block. */
    History h = {0};
    for (int i = 0; i < 4; i++) {
        history_add(&h, 5);
    }
    for (int i = 0; i < 4; i++) {
        history_add(&h, 0);
    }
    char out[128];
    panel_sparkline(out, sizeof(out), &h, 16);
    char expected[128];
    snprintf(expected, sizeof(expected), "%14s%s ", "", BLOCK[7]);
    CHECK(strcmp(out, expected) == 0);
}

static void test_sparkline_small_buffer(void)
{
    /* Room for two whole blocks and the '\0', not for half a third. */
    History h = {0};
    for (int i = 0; i < HISTORY; i++) {
        history_add(&h, 9);
    }
    char out[8];
    panel_sparkline(out, sizeof(out), &h, 16);
    CHECK(strlen(out) == 6);
    char tiny[1];
    panel_sparkline(tiny, sizeof(tiny), &h, 16);
    CHECK(tiny[0] == '\0');
}

/* A Sim with no creatures, no plants and soil everywhere, for placing
 * creatures by hand. */
static void make_empty_sim(Sim *sim)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "script");
    set.start_creatures = 0;
    set.min_creatures = 0;
    CHECK(sim_init(sim, &set, 7, 16, 16));
    for (int i = 0; i < 16 * 16; i++) {
        sim->world.terrain[i] = TERRAIN_SOIL;
    }
}

static void spawn_with_hue(Sim *sim, int x, float hue)
{
    int idx = pool_spawn(&sim->pool, &sim->world, x, 2);
    CHECK(idx >= 0);
    if (idx >= 0) {
        sim->pool.items[idx].genome.g[GENE_HUE] = hue;
    }
}

static void test_colour_groups(void)
{
    Sim sim;
    make_empty_sim(&sim);
    ColourGroup groups[PANEL_HUE_BANDS];
    groups[0].count = -1;
    CHECK(panel_colour_groups(&sim, groups, 2) == 0); /* nobody alive */
    CHECK(groups[0].count == -1);                     /* left alone */

    /* Three drawn in the azure band, one in red: inherited hues 0.50 to
     * 0.54 are drawn at 0.635 to 0.664 once the greens are skipped
     * (render_display_hue), and 0.10 at 0.073. */
    spawn_with_hue(&sim, 1, 0.50f);
    spawn_with_hue(&sim, 3, 0.52f);
    spawn_with_hue(&sim, 5, 0.54f);
    spawn_with_hue(&sim, 7, 0.10f);
    CHECK(panel_colour_groups(&sim, groups, 2) == 2);
    CHECK(groups[0].count == 3 && groups[1].count == 1);
    CHECK(strcmp(panel_band_name(groups[0].band), "azure") == 0);
    CHECK(strcmp(panel_band_name(groups[1].band), "red") == 0);
    /* Each group reports the average hue of its members. */
    CHECK(fabsf(groups[0].hue - 0.52f) < 1e-5f);
    CHECK(fabsf(groups[1].hue - 0.10f) < 1e-5f);

    /* Asking for more groups than exist returns only those. */
    CHECK(panel_colour_groups(&sim, groups, PANEL_HUE_BANDS) == 2);
    /* Asking for one gives the largest. */
    CHECK(panel_colour_groups(&sim, groups, 1) == 1 && groups[0].count == 3);
    sim_free(&sim);
}

static void test_band_names(void)
{
    CHECK(strcmp(panel_band_name(0), "red") == 0);
    CHECK(strcmp(panel_band_name(9), "violet") == 0);
    CHECK(strcmp(panel_band_name(PANEL_HUE_BANDS - 1), "pink") == 0);
    CHECK(strcmp(panel_band_name(-1), "?") == 0);
    CHECK(strcmp(panel_band_name(PANEL_HUE_BANDS), "?") == 0);
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_format_count();
    test_format_count_small_buffer();
    test_sparkline_empty();
    test_sparkline_newest_on_the_right();
    test_sparkline_ramp();
    test_sparkline_zero_is_blank();
    test_sparkline_small_buffer();
    test_colour_groups();
    test_band_names();
    TEST_SUMMARY();
}
