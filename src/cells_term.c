#include "cells.h"

#include <stddef.h> /* size_t */
#include <string.h> /* memcpy, memset, strlen */

#include "palette.h"
#include "term.h"

/*
 * The legend's colours, in RENDER_SWATCH_ order. Pointers, not copies:
 * an initialiser outside a function must be a constant expression, and
 * in C (unlike C++) the value of a const variable is not one, so
 * {PALETTE_WATER, PALETTE_ROCK, ...} would not compile here. The address of a static
 * variable is a constant.
 */
static const Colour *const SWATCHES[RENDER_SWATCH_COUNT] = {
    &PALETTE_WATER, &PALETTE_ROCK, &PALETTE_SOIL_DRY, &PALETTE_SOIL_RICH, &PALETTE_PLANT, &PALETTE_WITHERED, &PALETTE_CARRION,
};

void render_text(int col, int row, const char *text, int max_cols)
{
    char line[512];
    size_t width = max_cols > 0 ? (size_t)max_cols : 0;
    if (width > sizeof(line) - 1) {
        width = sizeof(line) - 1;
    }
    /* Only for ASCII: there one byte is one column on screen. A UTF-8
     * character of several bytes would be miscounted, or cut in half. */
    size_t len = strlen(text);
    if (len > width) {
        len = width;
    }
    memcpy(line, text, len);
    memset(line + len, ' ', width - len);
    line[width] = '\0';

    term_move(col, row);
    term_fg(PALETTE_TEXT.r, PALETTE_TEXT.g, PALETTE_TEXT.b);
    term_bg(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
            PALETTE_TEXT_BACKGROUND.b);
    term_put(line);
}

void render_utf8(int col, int row, const char *text)
{
    term_move(col, row);
    term_fg(PALETTE_TEXT.r, PALETTE_TEXT.g, PALETTE_TEXT.b);
    term_bg(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
            PALETTE_TEXT_BACKGROUND.b);
    term_put(text);
}

void render_swatch(int col, int row, int swatch)
{
    if (swatch < 0 || swatch >= RENDER_SWATCH_COUNT) {
        return; /* indexing SWATCHES with it would read out of bounds */
    }
    const Colour *c = SWATCHES[swatch];
    term_move(col, row);
    term_bg(c->r, c->g, c->b);
    term_put(" ");
}

void render_creature_swatch(int col, int row, float hue, float meat)
{
    /* The same function the map uses, so the legend cannot drift from
     * what is drawn. */
    Colour c = palette_creature(hue, meat);
    term_move(col, row);
    term_bg(c.r, c.g, c.b);
    term_put(" ");
}
