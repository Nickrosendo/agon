/* The window's version of cells.h. Linked with cells_canvas.c instead of the
 * terminal's cells_term.c: same function names, different file. */
#include "test.h"

#include <string.h> /* strlen */

#include "cells.h"
#include "cells_canvas.h"
#include "family.h"
#include "font.h"
#include "inspect.h"
#include "palette.h"
#include "panel.h"
#include "settings.h"
#include "sim.h"

#define W 400
#define H 100

static uint32_t pixel_of(Colour c)
{
    return canvas_rgb(c.r, c.g, c.b);
}

static int count_pixels(const Canvas *c, int x, int y, int w, int h,
                        uint32_t colour)
{
    int n = 0;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            n += c->px[j * c->w + i] == colour;
        }
    }
    return n;
}

static void test_text_cut_and_padded(void)
{
    Canvas c;
    CHECK(canvas_init(&c, W, H));
    cells_canvas_set(&c, 0, 0);
    uint32_t magenta = canvas_rgb(255, 0, 255);
    uint32_t fg = pixel_of(PALETTE_TEXT);
    uint32_t bg = pixel_of(PALETTE_TEXT_BACKGROUND);

    /* Cut: nothing is touched from column 3 on. */
    canvas_fill(&c, magenta);
    render_text(0, 0, "HHHHHH", 3);
    CHECK(count_pixels(&c, 0, 0, 3 * FONT_W, FONT_H, fg) > 0);
    CHECK(count_pixels(&c, 3 * FONT_W, 0, FONT_W, FONT_H, magenta) ==
          FONT_W * FONT_H);

    /* Padded: the columns past the text are all background. */
    canvas_fill(&c, magenta);
    render_text(0, 0, "H", 4);
    CHECK(count_pixels(&c, FONT_W, 0, 3 * FONT_W, FONT_H, bg) ==
          3 * FONT_W * FONT_H);
    CHECK(count_pixels(&c, 4 * FONT_W, 0, FONT_W, FONT_H, magenta) ==
          FONT_W * FONT_H);
    canvas_free(&c);
}

static void test_origin_moves_everything(void)
{
    Canvas c;
    CHECK(canvas_init(&c, W, H));
    uint32_t magenta = canvas_rgb(255, 0, 255);
    canvas_fill(&c, magenta);
    cells_canvas_set(&c, 100, 20);
    render_text(1, 1, "H", 1);
    /* Nothing where the cell would be with no origin. */
    CHECK(count_pixels(&c, FONT_W, FONT_H, FONT_W, FONT_H, magenta) ==
          FONT_W * FONT_H);
    /* The cell, at origin + (col, row). */
    CHECK(count_pixels(&c, 100 + FONT_W, 20 + FONT_H, FONT_W, FONT_H, magenta) ==
          0);
    canvas_free(&c);
}

static void test_sparkline_blocks_decode(void)
{
    Canvas c;
    CHECK(canvas_init(&c, W, H));
    cells_canvas_set(&c, 0, 0);
    uint32_t fg = pixel_of(PALETTE_TEXT);
    /* The eight blocks are E2 96 81 to E2 96 88. Each must take one column
     * and a bar as tall as its number of eighths. */
    for (int eighths = 1; eighths <= 8; eighths++) {
        char text[4] = {'\xE2', '\x96', (char)(0x80 + eighths), '\0'};
        canvas_fill(&c, canvas_rgb(0, 0, 0));
        render_utf8(0, 0, text);
        int want = FONT_W * (FONT_H * eighths / 8);
        CHECK(count_pixels(&c, 0, 0, FONT_W, FONT_H, fg) == want);
    }
    /* Two blocks and a space: three columns, not nine for nine bytes. */
    canvas_fill(&c, canvas_rgb(0, 0, 0));
    render_utf8(0, 0, "\xE2\x96\x88\xE2\x96\x88 ");
    CHECK(count_pixels(&c, 0, 0, FONT_W, FONT_H, fg) == FONT_W * FONT_H);
    CHECK(count_pixels(&c, FONT_W, 0, FONT_W, FONT_H, fg) == FONT_W * FONT_H);
    CHECK(count_pixels(&c, 2 * FONT_W, 0, FONT_W, FONT_H, fg) == 0);
    canvas_free(&c);
}

static void test_swatch_uses_map_colour(void)
{
    Canvas c;
    CHECK(canvas_init(&c, W, H));
    cells_canvas_set(&c, 0, 0);
    /* The legend shows the map's symbols, so not every pixel is the colour. */
    canvas_fill(&c, canvas_rgb(255, 0, 255));
    render_swatch(0, 0, RENDER_SWATCH_WATER);
    CHECK(count_pixels(&c, 0, 0, FONT_W, FONT_H, pixel_of(PALETTE_WATER)) > 0);
    canvas_fill(&c, canvas_rgb(255, 0, 255));
    render_swatch(1, 0, RENDER_SWATCH_CARRION);
    CHECK(count_pixels(&c, FONT_W, 0, FONT_W, FONT_H, pixel_of(PALETTE_CARRION)) >
          0);
    /* And no magenta left in its cell: it clears what it covers. */
    CHECK(count_pixels(&c, FONT_W, 0, FONT_W, FONT_H, canvas_rgb(255, 0, 255)) ==
          0);
    /* Every known swatch draws something in its own colour. */
    const Colour *colours[RENDER_SWATCH_COUNT] = {
        &PALETTE_WATER, &PALETTE_ROCK, &PALETTE_SOIL_DRY, &PALETTE_SOIL_RICH,
        &PALETTE_PLANT, &PALETTE_WITHERED, &PALETTE_CARRION,
    };
    for (int s = 0; s < RENDER_SWATCH_COUNT; s++) {
        canvas_fill(&c, canvas_rgb(0, 0, 0));
        render_swatch(0, 0, s);
        CHECK(count_pixels(&c, 0, 0, FONT_W, FONT_H, pixel_of(*colours[s])) > 0);
    }
    canvas_free(&c);
}

static void test_unknown_swatch_draws_nothing(void)
{
    Canvas c;
    CHECK(canvas_init(&c, W, H));
    cells_canvas_set(&c, 0, 0);
    uint32_t magenta = canvas_rgb(255, 0, 255);
    canvas_fill(&c, magenta);
    render_swatch(0, 0, -1);
    render_swatch(0, 0, RENDER_SWATCH_COUNT);
    CHECK(count_pixels(&c, 0, 0, W, H, magenta) == W * H);
    canvas_free(&c);
}

static void test_off_the_canvas_is_clipped(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 4, 4));
    cells_canvas_set(&c, 0, 0);
    render_text(100, 100, "off the canvas", 20);
    render_swatch(-5, -5, RENDER_SWATCH_ROCK);
    render_creature_swatch(50, 50, 0.5f, 0.5f);
    render_utf8(100, 100, "\xE2\x96\x88");
    canvas_free(&c);
    /* And an empty canvas, as when the window is minimised. */
    CHECK(canvas_init(&c, 0, 0));
    cells_canvas_set(&c, 0, 0);
    render_text(0, 0, "nothing", 7);
    render_swatch(0, 0, RENDER_SWATCH_ROCK);
    canvas_free(&c);
}

static uint64_t hash_pixels(const Canvas *c)
{
    uint64_t h = 14695981039346656037u;
    for (int i = 0; i < c->w * c->h; i++) {
        h ^= c->px[i];
        h *= 1099511628211u;
    }
    return h;
}

/*
 * After the last death the panels and the family strip must still draw, reading
 * nothing about a dead creature. A dead slot's fields stay readable, so reading
 * one would go unnoticed by the memory checkers. So: draw, fill every dead slot
 * with nonsense, draw again, and the pictures must match.
 */
static void test_panel_and_family_on_dead_world(void)
{
    Settings set = settings_default();
    set.stats_every = 1;
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    for (int t = 0; t < 20; t++) {
        sim_step(&s);
        family_update(&f, &s);
    }
    /* Following the creature in the first living slot, as it dies. */
    Inspect in = {0};
    int followed = 0;
    while (!s.pool.items[followed].alive) {
        followed++;
    }
    in.on = in.following = true;
    in.id = s.pool.items[followed].id;
    for (int i = 0; i < s.pool.capacity; i++) {
        if (s.pool.items[i].alive) {
            pool_kill(&s.pool, &s.world, i, &s.set);
        }
    }
    sim_step(&s);
    CHECK(sim_is_over(&s));
    family_update(&f, &s);
    inspect_update(&in, &s);
    CHECK(inspect_creature(&in, &s) == NULL);
    CHECK(family_largest(&f, &s) == FAMILY_NONE);

    Canvas c;
    CHECK(canvas_init(&c, PANEL_COLS * FONT_W, 40 * FONT_H));
    cells_canvas_set(&c, 0, 0);
    panel_draw(&s, 0, 0, 40);
    uint64_t plain = hash_pixels(&c);
    panel_draw_inspect(&s, &in, 0, 0, 40);
    uint64_t inspected = hash_pixels(&c);

    for (int i = 0; i < s.pool.capacity; i++) {
        memset(&s.pool.items[i], 0xFF, sizeof(s.pool.items[i]));
        s.pool.items[i].alive = false;
    }
    panel_draw(&s, 0, 0, 40);
    CHECK(hash_pixels(&c) == plain);
    panel_draw_inspect(&s, &in, 0, 0, 40);
    CHECK(hash_pixels(&c) == inspected);
    family_update(&f, &s);
    CHECK(family_largest(&f, &s) == FAMILY_NONE);
    canvas_free(&c);
    family_free(&f);
    sim_free(&s);
}

/* The label as text at (col, row) on an empty canvas equals the same pixels on
 * the panel: the click rows and the words on screen cannot drift apart unseen. */
static bool panel_row_says(Canvas *panel, int row, const char *label)
{
    enum { LABEL_COL = 4 }; /* the two swatch columns, the indent and a space */
    Canvas expected;
    CHECK(canvas_init(&expected, PANEL_COLS * FONT_W, FONT_H));
    cells_canvas_set(&expected, 0, 0);
    render_text(LABEL_COL, 0, label, (int)strlen(label));
    bool same = true;
    for (int y = 0; y < FONT_H; y++) {
        for (int x = LABEL_COL * FONT_W;
             x < (LABEL_COL + (int)strlen(label)) * FONT_W; x++) {
            same = same && expected.px[y * expected.w + x] ==
                               panel->px[(row * FONT_H + y) * panel->w + x];
        }
    }
    canvas_free(&expected);
    /* The window's cells remember the canvas they were last given, and this one
     * is gone: drawing now would write into freed memory. So point them back. */
    cells_canvas_set(panel, 0, 0);
    return same;
}

static void test_clickable_rows_hold_their_words(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Canvas c;
    CHECK(canvas_init(&c, PANEL_COLS * FONT_W, 40 * FONT_H));
    cells_canvas_set(&c, 0, 0);
    panel_draw(&s, 0, 0, 40);
    CHECK(panel_row_says(&c, PANEL_ROW_TOP_HUNTER, "top hunter"));
    CHECK(panel_row_says(&c, PANEL_ROW_OLDEST, "oldest"));
    CHECK(!panel_row_says(&c, PANEL_ROW_OLDEST, "top hunter"));
    /* Refused births show only once there are some, right below. */
    CHECK(!panel_row_says(&c, PANEL_ROW_OLDEST + 1, "refused"));
    s.pool.births_refused = 3;
    panel_draw(&s, 0, 0, 40);
    CHECK(panel_row_says(&c, PANEL_ROW_OLDEST + 1, "refused"));
    CHECK(panel_row_says(&c, PANEL_ROW_OLDEST, "oldest"));
    canvas_free(&c);
    sim_free(&s);
}

int main(void)
{
    test_text_cut_and_padded();
    test_origin_moves_everything();
    test_sparkline_blocks_decode();
    test_swatch_uses_map_colour();
    test_unknown_swatch_draws_nothing();
    test_off_the_canvas_is_clipped();
    test_panel_and_family_on_dead_world();
    test_clickable_rows_hold_their_words();
    TEST_SUMMARY();
}
