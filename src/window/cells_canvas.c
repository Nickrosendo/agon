#include "cells_canvas.h"

#include <stddef.h> /* size_t */
#include <string.h> /* memcpy, memset, strlen */

#include "cells.h"
#include "font.h"
#include "palette.h"

static Canvas *target;

/* The legend's colours, as in cells_term.c. */
static const Colour *const SWATCHES[RENDER_SWATCH_COUNT] = {
    &PALETTE_WATER, &PALETTE_ROCK, &PALETTE_SOIL_DRY, &PALETTE_SOIL_RICH,
    &PALETTE_PLANT, &PALETTE_WITHERED, &PALETTE_CARRION,
};

static uint32_t to_pixel(Colour c)
{
    return canvas_rgb(c.r, c.g, c.b);
}

void cells_canvas_bind(Canvas *c)
{
    target = c;
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
    font_text(target, col * FONT_W, row * FONT_H, line, to_pixel(PALETTE_TEXT),
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
    int x = col * FONT_W;
    int y = row * FONT_H;
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
    canvas_rect(target, col * FONT_W, row * FONT_H, FONT_W, FONT_H,
                to_pixel(*SWATCHES[swatch]));
}

void render_creature_swatch(int col, int row, float hue, float meat)
{
    canvas_rect(target, col * FONT_W, row * FONT_H, FONT_W, FONT_H,
                to_pixel(palette_creature(hue, meat)));
}
