/* The window's version of cells.h. Linked with cells_canvas.c instead of the
 * terminal's cells_term.c: same function names, different file. */
#include "test.h"

#include <string.h> /* strlen */

#include "cells.h"
#include "cells_canvas.h"
#include "font.h"
#include "palette.h"

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

int main(void)
{
    test_text_cut_and_padded();
    test_origin_moves_everything();
    test_sparkline_blocks_decode();
    test_swatch_uses_map_colour();
    test_unknown_swatch_draws_nothing();
    test_off_the_canvas_is_clipped();
    TEST_SUMMARY();
}
