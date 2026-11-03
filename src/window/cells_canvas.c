#include "cells_canvas.h"

#include <stddef.h> /* size_t */
#include <string.h> /* memcpy, memset, strlen */

#include "cells.h"
#include "font.h"
#include "palette.h"
#include "tiles.h"
#include "world.h"  /* VEG_MAX */

static Canvas *target;
static int origin_x, origin_y;

/* What the legend shows for each swatch: the map's own symbol, in the map's
 * own colour, so the legend looks like what it explains. */
typedef struct {
    const Colour *colour;
    int tile, detail;
} SwatchLook;

static const SwatchLook SWATCHES[RENDER_SWATCH_COUNT] = {
    {&PALETTE_WATER, TILE_WATER, 0},
    {&PALETTE_ROCK, TILE_ROCK, 0},
    {&PALETTE_SOIL_DRY, TILE_SOIL, 0},
    {&PALETTE_SOIL_RICH, TILE_SOIL, TILE_SOIL_DETAILS - 1},
    {&PALETTE_PLANT, TILE_PLANT, VEG_MAX},
    {&PALETTE_WITHERED, TILE_PLANT, VEG_MAX},
    {&PALETTE_CARRION, TILE_CARRION, 0},
};

/* The pixel a character cell starts at. */
static int cell_x(int col)
{
    return origin_x + col * FONT_W;
}

static int cell_y(int row)
{
    return origin_y + row * FONT_H;
}

static uint32_t to_pixel(Colour c)
{
    return canvas_rgb(c.r, c.g, c.b);
}

void cells_canvas_set(Canvas *c, int x, int y)
{
    target = c;
    origin_x = x;
    origin_y = y;
}

void render_text(int col, int row, const char *text, int max_cols)
{
    char line[512];
    size_t width = max_cols > 0 ? (size_t)max_cols : 0;
    if (width > sizeof(line) - 1) {
        width = sizeof(line) - 1;
    }
    size_t len = strlen(text);
    if (len > width) {
        len = width;
    }
    memcpy(line, text, len);
    memset(line + len, ' ', width - len);
    line[width] = '\0';
    font_text(target, cell_x(col), cell_y(row), line, to_pixel(PALETTE_TEXT),
              to_pixel(PALETTE_TEXT_BACKGROUND));
}

/*
 * The panel's UTF-8 is spaces and the eight block heights, U+2581 to U+2588,
 * whose three bytes are E2 96 81 to E2 96 88. Anything else draws as '?'.
 * A UTF-8 character is several bytes, so i moves by what each one took.
 */
void render_utf8(int col, int row, const char *text)
{
    uint32_t fg = to_pixel(PALETTE_TEXT);
    uint32_t bg = to_pixel(PALETTE_TEXT_BACKGROUND);
    int x = cell_x(col);
    int y = cell_y(row);
    size_t i = 0;
    while (text[i] != '\0') {
        /* unsigned char: on most machines char is signed, and 0xE2 would
         * come out negative and never equal 0xE2. */
        const unsigned char *b = (const unsigned char *)text + i;
        if (b[0] == 0xE2 && b[1] == 0x96 && b[2] >= 0x81 && b[2] <= 0x88) {
            font_block(target, x, y, b[2] - 0x80, fg, bg);
            i += 3;
        } else {
            char one[2] = {b[0] < 0x80 ? text[i] : '?', '\0'};
            font_text(target, x, y, one, fg, bg);
            i += 1;
        }
        x += FONT_W;
    }
}

void render_swatch(int col, int row, int swatch)
{
    if (swatch < 0 || swatch >= RENDER_SWATCH_COUNT) {
        return;
    }
    const SwatchLook *look = &SWATCHES[swatch];
    /* An 8 by 8 symbol, in the middle of a cell twice as tall as it is wide. */
    uint16_t rows[16];
    tiles_build(rows, look->tile, 0, look->detail, 8, 0);
    canvas_rect(target, cell_x(col), cell_y(row), FONT_W, FONT_H,
                to_pixel(PALETTE_TEXT_BACKGROUND));
    canvas_mask(target, rows, 8, cell_x(col), cell_y(row) + (FONT_H - 8) / 2, 1,
                false, to_pixel(*look->colour));
}

void render_creature_swatch(int col, int row, float hue, float meat)
{
    canvas_rect(target, cell_x(col), cell_y(row), FONT_W, FONT_H,
                to_pixel(palette_creature(hue, meat)));
}
