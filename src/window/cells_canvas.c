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

/* What the legend shows for each swatch, in the map's own colour, so the
 * legend looks like what it explains: a square of ground, a plant, or bones. */
enum { SWATCH_GROUND, SWATCH_PLANT, SWATCH_BONES };
typedef struct {
    const Colour *colour;
    int shape;
} SwatchLook;

static const SwatchLook SWATCHES[RENDER_SWATCH_COUNT] = {
    {&PALETTE_WATER, SWATCH_GROUND},
    {&PALETTE_ROCK, SWATCH_GROUND},
    {&PALETTE_SOIL_DRY, SWATCH_GROUND},
    {&PALETTE_SOIL_RICH, SWATCH_GROUND},
    {&PALETTE_PLANT, SWATCH_PLANT},
    {&PALETTE_WITHERED, SWATCH_PLANT},
    {&PALETTE_CARRION, SWATCH_BONES},
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
    uint16_t rows[16] = {0}; /* = {0} sets every element, not just the first */
    if (look->shape == SWATCH_PLANT) {
        /* The small tree at full density, its 8 by 8 around the foot, as it
         * stands in a cell: the foot 3 across and 6 down. */
        TreePicture tree;
        tiles_build_tree(&tree, VEG_MAX, false, false, 0, 0, 8);
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < 8; i++) {
                int x = tree.foot_x - 3 + i, y = tree.foot_y - 6 + j;
                if (tree.ink[y * TREE_W + x] != TREE_NONE) {
                    rows[j] = (uint16_t)(rows[j] | (1u << (7 - i)));
                }
            }
        }
    } else if (look->shape == SWATCH_BONES) {
        uint8_t ink[TILE_MAX * TILE_MAX];
        tiles_build_bones(ink, 0, 8);
        for (int j = 0; j < 8; j++) {
            for (int i = 0; i < 8; i++) {
                if (ink[j * 8 + i] != 0) {
                    rows[j] = (uint16_t)(rows[j] | (1u << (7 - i)));
                }
            }
        }
    } else {
        for (int j = 0; j < 8; j++) {
            rows[j] = 0xFF; /* all eight pixels of the row */
        }
    }
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
