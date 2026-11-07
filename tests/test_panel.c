#include "test.h"

#include <math.h>    /* fabsf */
#include <stdbool.h>
#include <stdint.h> /* int64_t, INT64_C */
#include <string.h> /* strcmp */

#include "brain_script.h"
#include "chronicle.h"
#include "inspect.h"
#include "lifepage.h"
#include "panel.h"
#include "settings.h"
#include "sim.h"

static int formats_as(int64_t n, const char *expected)
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

/*
 * A tick count past 2^32. long is 64 bits on Linux, so there this passes with
 * long or without it; the check that can really fail runs on Windows, where
 * long is 32 bits and these numbers would come out cut.
 */
static void test_large_tick_count_not_cut(void)
{
    CHECK(formats_as(INT64_C(5000000000), "5,000,000,000"));
    CHECK(formats_as(INT64_MAX, "9,223,372,036,854,775,807"));
    Sim sim = {.tick = UINT64_C(4294967296) + 7};
    char out[32];
    panel_format_count(out, sizeof(out), (int64_t)sim.tick);
    CHECK(strcmp(out, "4,294,967,303") == 0);
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
    set.start_share = 0.0f;
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

/* ---- the life log, the L page and the announcement ---- */

/* True when one of the n lines contains text. */
static bool lines_have(char lines[][PANEL_LINE_BYTES], int n, const char *text)
{
    for (int k = 0; k < n; k++) {
        if (strstr(lines[k], text) != NULL) {
            return true;
        }
    }
    return false;
}

/* The same for the page's longer lines. A two-dimensional array parameter must
 * name its row length, so each line width needs a function of its own. */
static bool page_has(char lines[][LIFE_PAGE_LINE_BYTES], int n, const char *text)
{
    for (int k = 0; k < n; k++) {
        if (strstr(lines[k], text) != NULL) {
            return true;
        }
    }
    return false;
}

/* A creature at (4, 4) with plenty of energy and health, noted as born. */
static int spawn_living(Sim *sim)
{
    int slot = pool_spawn(&sim->pool, &sim->world, 4, 4);
    CHECK(slot >= 0);
    Creature *c = &sim->pool.items[slot];
    c->energy = 1000.0f;
    c->health = 1000.0f;
    chronicle_note(&sim->chronicle, c,
                   (LifeEvent){.tick = 0, .kind = LIFE_BORN, .value = 0});
    return slot;
}

/* kills kills of creatures with ids 900, 901, ..., on ticks 100, 200, ... */
static void note_kills(Sim *sim, Creature *c, int kills)
{
    for (int k = 0; k < kills; k++) {
        chronicle_note(&sim->chronicle, c,
                       (LifeEvent){.tick = (uint32_t)(100 * (k + 1)),
                                   .kind = LIFE_KILL,
                                   .other_id = 900u + (uint32_t)k});
    }
}

static void test_panel_shows_name_title_age_health_kills_log(void)
{
    Sim sim;
    make_empty_sim(&sim);
    int slot = spawn_living(&sim);
    Creature *c = &sim.pool.items[slot];
    note_kills(&sim, c, 12);
    c->age = 250;
    Inspect in = {0};
    inspect_follow(&in, &sim, slot);

    char lines[PANEL_INSPECT_LINES][PANEL_LINE_BYTES];
    int n = panel_inspect_text(&sim, &in, lines, PANEL_INSPECT_LINES);
    char name[16], killed[32], last[16];
    name_from_id(c->id, name, sizeof(name));
    name_from_id(911, last, sizeof(last)); /* the twelfth kill */
    snprintf(killed, sizeof(killed), "killed %s", last);
    CHECK(lines_have(lines, n, name));
    CHECK(lines_have(lines, n, "the Slayer"));
    CHECK(lines_have(lines, n, "age"));
    CHECK(lines_have(lines, n, "health"));
    CHECK(lines_have(lines, n, "kills"));
    CHECK(lines_have(lines, n, "12"));
    CHECK(lines_have(lines, n, killed)); /* the newest, in the short log */
    /* Every line fits the panel. */
    for (int k = 0; k < n; k++) {
        CHECK(strlen(lines[k]) <= PANEL_COLS);
    }
    sim_free(&sim);
}

/* A sleeping creature's panel says so, and what it is doing; an awake one's
 * does not. */
static void test_panel_shows_hibernating(void)
{
    Sim sim;
    make_empty_sim(&sim);
    int slot = spawn_living(&sim);
    Creature *c = &sim.pool.items[slot];
    Inspect in = {0};
    inspect_follow(&in, &sim, slot);
    char lines[PANEL_INSPECT_LINES][PANEL_LINE_BYTES];
    int n = panel_inspect_text(&sim, &in, lines, PANEL_INSPECT_LINES);
    CHECK(!lines_have(lines, n, "hibernat"));
    c->current = (Action){.kind = ACT_SLEEP, .hold = 100};
    n = panel_inspect_text(&sim, &in, lines, PANEL_INSPECT_LINES);
    CHECK(lines_have(lines, n, "hibernating"));
    CHECK(lines_have(lines, n, "hibernate")); /* the "doing" line */
    for (int k = 0; k < n; k++) {
        CHECK(strlen(lines[k]) <= PANEL_COLS);
    }
    sim_free(&sim);
}

static void test_l_page_lists_a_whole_life(void)
{
    Sim sim;
    make_empty_sim(&sim);
    int slot = spawn_living(&sim);
    Creature *c = &sim.pool.items[slot];
    note_kills(&sim, c, 20); /* more than a creature's own ring of 8 */
    LifePage page;
    life_page_open(&page, &sim, c->id);
    CHECK(page.open && page.known && page.alive && page.in_table);
    CHECK(page.n_events == 21); /* the birth and 20 kills, oldest first */
    CHECK(page.events[0].kind == LIFE_BORN);

    char lines[LIFE_PAGE_MAX_LINES][LIFE_PAGE_LINE_BYTES];
    int n = life_page_text(&page, sim.set.ticks_per_year, 80, lines,
                           LIFE_PAGE_MAX_LINES);
    char name[16];
    name_from_id(c->id, name, sizeof(name));
    CHECK(page_has(lines, n, name));
    CHECK(page_has(lines, n, "a founder"));
    CHECK(page_has(lines, n, "kills 20"));
    /* The birth once, in the heading, not again in the story. */
    int births = 0;
    for (int k = 0; k < n; k++) {
        births += strstr(lines[k], "a founder") != NULL;
    }
    CHECK(births == 1);
    CHECK(page_has(lines, n, "kill 1"));  /* the first kill */
    CHECK(page_has(lines, n, "kill 20")); /* and the last */
    /* Too few lines for every event: the newest stay, and the page says so. */
    n = life_page_text(&page, sim.set.ticks_per_year, 80, lines, 14);
    CHECK(n <= 14);
    CHECK(page_has(lines, n, "a founder")); /* the heading says it now */
    CHECK(page_has(lines, n, "kill 20"));
    CHECK(!page_has(lines, n, "kill 1,"));
    CHECK(page_has(lines, n, "earlier"));
    sim_free(&sim);
}

static void test_l_page_when_followed_creature_dies(void)
{
    Sim sim;
    make_empty_sim(&sim);
    int slot = spawn_living(&sim);
    Creature *c = &sim.pool.items[slot];
    uint32_t id = c->id;
    note_kills(&sim, c, 3);
    LifePage page;
    life_page_open(&page, &sim, id);
    /* No energy and a sliver of health: hunger takes the rest in the next
     * tick. Health already at 0 would count as an attack's work. */
    c->energy = 0.0f;
    c->health = 0.001f;
    sim_step(&sim);
    CHECK(!sim.pool.items[slot].alive);
    /* A newborn takes the slot: the page must not show it. */
    int other = pool_spawn(&sim.pool, &sim.world, 8, 8);
    CHECK(other == slot);
    life_page_update(&page, &sim);
    CHECK(page.open && page.known && !page.alive && page.id == id);
    char lines[LIFE_PAGE_MAX_LINES][LIFE_PAGE_LINE_BYTES];
    int n = life_page_text(&page, sim.set.ticks_per_year, 80, lines,
                           LIFE_PAGE_MAX_LINES);
    CHECK(page_has(lines, n, "died of hunger"));
    CHECK(page_has(lines, n, "kill 3"));
    /* Even with the table emptied, the page keeps what it last read. */
    chronicle_clear(&sim.chronicle);
    life_page_update(&page, &sim);
    n = life_page_text(&page, sim.set.ticks_per_year, 80, lines,
                       LIFE_PAGE_MAX_LINES);
    CHECK(page.known && page_has(lines, n, "died of hunger"));
    sim_free(&sim);
}

static void test_l_page_most_notable(void)
{
    Sim sim;
    make_empty_sim(&sim);
    LifePage page;
    CHECK(!life_page_open_most_notable(&page, &sim)); /* nothing recorded */
    CHECK(!page.open);
    int a = spawn_living(&sim);
    int b = pool_spawn(&sim.pool, &sim.world, 9, 9);
    note_kills(&sim, &sim.pool.items[b], 5);
    CHECK(life_page_open_most_notable(&page, &sim));
    CHECK(page.id == sim.pool.items[b].id);
    CHECK(page.id != sim.pool.items[a].id);
    sim_free(&sim);
}

/* One creature of size 2.5 in a world whose giants start at 2. */
static int spawn_large(Sim *sim)
{
    sim->set.giant_size = 2.0f;
    int slot = spawn_living(sim);
    Genome *g = &sim->pool.items[slot].genome;
    g->g[GENE_SIZE] = 0.5f; /* size 1 at birth, as in the chronicle's tests */
    sim->pool.items[slot].grown = 1.5f;
    return slot;
}

static void test_announce_once(void)
{
    Sim sim;
    make_empty_sim(&sim);
    Announce an;
    announce_reset(&an);
    char text[128];
    CHECK(!announce_check(&an, &sim, text, sizeof(text)));
    int slot = spawn_large(&sim);
    Creature *c = &sim.pool.items[slot];
    sim_step(&sim);
    CHECK(announce_check(&an, &sim, text, sizeof(text)));
    char name[16];
    name_from_id(c->id, name, sizeof(name));
    CHECK(strstr(text, name) != NULL);
    CHECK(strstr(text, "year") != NULL);
    CHECK(!announce_check(&an, &sim, text, sizeof(text))); /* only once */
    /* Shrinking below the line and crossing it again is not news. */
    c->grown = 0.0f;
    sim_step(&sim);
    c->grown = 1.5f;
    sim_step(&sim);
    sim_step(&sim);
    CHECK(!announce_check(&an, &sim, text, sizeof(text)));
    sim_free(&sim);
}

static void test_announce_jump_follows(void)
{
    Sim sim;
    make_empty_sim(&sim);
    Announce an;
    announce_reset(&an);
    CHECK(announce_slot(&an, &sim) == -1);
    int slot = spawn_large(&sim);
    sim_step(&sim);
    char text[128];
    CHECK(announce_check(&an, &sim, text, sizeof(text)));
    CHECK(announce_slot(&an, &sim) == slot);
    Inspect in = {0};
    inspect_follow(&in, &sim, announce_slot(&an, &sim));
    CHECK(inspect_creature(&in, &sim) == &sim.pool.items[slot]);
    /* Once it has died there is nobody to jump to. */
    sim.pool.items[slot].energy = 0.0f;
    sim.pool.items[slot].health = 0.001f;
    sim_step(&sim);
    CHECK(announce_slot(&an, &sim) == -1);
    sim_free(&sim);
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_format_count();
    test_large_tick_count_not_cut();
    test_format_count_small_buffer();
    test_sparkline_empty();
    test_sparkline_newest_on_the_right();
    test_sparkline_ramp();
    test_sparkline_zero_is_blank();
    test_sparkline_small_buffer();
    test_colour_groups();
    test_band_names();
    test_panel_shows_name_title_age_health_kills_log();
    test_panel_shows_hibernating();
    test_l_page_lists_a_whole_life();
    test_l_page_when_followed_creature_dies();
    test_l_page_most_notable();
    test_announce_once();
    test_announce_jump_follows();
    TEST_SUMMARY();
}
