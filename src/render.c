#include "render.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* memset */

#include "cells.h"
#include "genome.h"
#include "palette.h"
#include "term.h"

/* The upper half block, U+2580, as its three UTF-8 bytes. The top half of
 * the character cell takes the foreground colour, the bottom half the
 * background colour, so one character shows two square cells. */
#define UPPER_HALF_BLOCK "\xE2\x96\x80"

/* The largest screen area whose last drawing is remembered. Cells beyond
 * it are simply redrawn every frame. */
#define MEMORY_COLS 512
#define MEMORY_ROWS 256

/*
 * What render_world last drew in each character cell: the top and bottom
 * colours packed into one number, with bit 48 set to mark "drawn". A
 * cell whose colours have not changed since the last frame is skipped,
 * so a calm world sends only a few bytes per frame instead of a full
 * screen. A static array: 1 MiB set aside for the whole run, zeroed at
 * start-up (zero means "never drawn"), never allocated or freed.
 */
static uint64_t last_drawn[MEMORY_ROWS][MEMORY_COLS];

/* A struct is compared field by field; C has no == for structs. */
static int same_colour(Colour a, Colour b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static Colour cell_colour(const Sim *s, int i)
{
    const World *w = &s->world;
    const Settings *set = &s->set;
    /* A creature hides whatever is under it. occupant holds its slot in
     * the pool, so its genome is one lookup away. */
    if (w->occupant[i] >= 0) {
        const Creature *c = &s->pool.items[w->occupant[i]];
        return palette_creature(c->genome.g[GENE_HUE],
                               render_meat_share(c->level));
    }
    if (w->terrain[i] == TERRAIN_WATER) {
        return PALETTE_WATER;
    }
    if (w->terrain[i] == TERRAIN_ROCK) {
        return PALETTE_ROCK;
    }
    Colour soil = palette_soil(w->fertility[i]);
    if (w->carrion[i] > 0.0f) {
        /* Carrion covers whatever grows there: from a faint stain to
         * full colour as it approaches several bites' worth, in a few
         * steps so neighbouring cells often share a colour. */
        float amount = w->carrion[i] / (w->carrion[i] + set->bite_meat_energy);
        int shade = 1 + (int)(amount * 3.0f); /* 1..3, 4 only at amount 1 */
        return palette_mix(soil, PALETTE_CARRION, (float)shade / 4.0f);
    }
    if (w->veg[i] == 0) {
        return soil;
    }
    /* Four shades of the rule's green over the soil: density 1 is 40 %
     * of the way from the soil colour to the full green, density VEG_MAX
     * is all green. */
    float t = (float)(w->veg[i] + 1) / (float)(VEG_MAX + 1);
    Colour plant = palette_mix(soil, palette_plant(w->veg_rule[i]), t);
    if (w->veg_wither[i] == 0) {
        return plant;
    }
    /* Withering: from the plant's green towards straw, in a few steps. */
    int shade = palette_wither_shade(w->veg_wither[i], set->veg_wither_ticks);
    return palette_mix(plant, PALETTE_WITHERED,
                       (float)shade / (float)PALETTE_WITHER_SHADES);
}

static uint64_t pack(Colour top, Colour bottom)
{
    /* The casts widen each byte to 64 bits before shifting; shifting a
     * value past the width of its type is undefined behaviour. */
    uint64_t t = ((uint64_t)top.r << 16) | ((uint64_t)top.g << 8) | top.b;
    uint64_t b =
        ((uint64_t)bottom.r << 16) | ((uint64_t)bottom.g << 8) | bottom.b;
    return (1ULL << 48) | (t << 24) | b;
}

void render_invalidate(void)
{
    /* memset with sizeof works here because last_drawn is a real array
     * in this file, not a pointer: sizeof gives all 1 MiB. */
    memset(last_drawn, 0, sizeof(last_drawn));
}

/* One character cell showing two cells, top and bottom. */
static void put_cell(Colour top, Colour bottom)
{
    if (same_colour(top, bottom)) {
        /* A space in the background colour looks the same and is 1 byte
         * instead of 3. */
        term_bg(top.r, top.g, top.b);
        term_put(" ");
    } else {
        term_fg(top.r, top.g, top.b);
        term_bg(bottom.r, bottom.g, bottom.b);
        term_put(UPPER_HALF_BLOCK);
    }
}

void render_world(const Sim *s, int col0, int row0, int cols, int rows)
{
    const World *w = &s->world;
    int view_cols = cols < w->w ? cols : w->w;
    int view_rows = rows < w->h / 2 ? rows : w->h / 2;
    for (int row = 0; row < view_rows; row++) {
        /* The cursor is only moved when a cell is skipped: after a
         * character is drawn, the terminal moves it one column right by
         * itself. */
        int cursor_in_place = 0;
        for (int x = 0; x < view_cols; x++) {
            Colour top = cell_colour(s, world_idx(w, x, 2 * row));
            Colour bottom = cell_colour(s, world_idx(w, x, 2 * row + 1));
            uint64_t packed = pack(top, bottom);
            int remembered = row < MEMORY_ROWS && x < MEMORY_COLS;
            if (remembered && last_drawn[row][x] == packed) {
                cursor_in_place = 0;
                continue;
            }
            if (remembered) {
                last_drawn[row][x] = packed;
            }
            if (!cursor_in_place) {
                term_move(col0 + x, row0 + row);
                cursor_in_place = 1;
            }
            put_cell(top, bottom);
        }
    }
}

void render_cursor(const Sim *s, int col0, int row0, int cols, int rows,
                   int x, int y)
{
    const World *w = &s->world;
    int row = y / 2; /* the character row holding cells 2 row, 2 row + 1 */
    if (x < 0 || x >= cols || x >= w->w || row < 0 || row >= rows ||
        y >= w->h) {
        return;
    }
    Colour top = cell_colour(s, world_idx(w, x, 2 * row));
    Colour bottom = cell_colour(s, world_idx(w, x, 2 * row + 1));
    /* y % 2 picks the half: 0 the top cell, 1 the bottom one. */
    if (y % 2 == 0) {
        top = PALETTE_CURSOR;
    } else {
        bottom = PALETTE_CURSOR;
    }
    term_move(col0 + x, row0 + row);
    term_fg(top.r, top.g, top.b);
    term_bg(bottom.r, bottom.g, bottom.b);
    term_put(UPPER_HALF_BLOCK);
    /* 0 means "never drawn": render_world will draw this cell again on
     * the next frame, whatever its colours. */
    if (row < MEMORY_ROWS && x < MEMORY_COLS) {
        last_drawn[row][x] = 0;
    }
}

void render_snapshot(const Sim *s)
{
    const World *w = &s->world;
    term_begin();
    /* (h + 1) / 2 rows, so an odd height still shows its last row; the
     * missing cell below it is drawn in the terminal's own colour. */
    for (int row = 0; row < (w->h + 1) / 2; row++) {
        for (int x = 0; x < w->w; x++) {
            Colour top = cell_colour(s, world_idx(w, x, 2 * row));
            if (2 * row + 1 < w->h) {
                put_cell(top, cell_colour(s, world_idx(w, x, 2 * row + 1)));
            } else {
                term_reset_colours();
                term_fg(top.r, top.g, top.b);
                term_put(UPPER_HALF_BLOCK);
            }
        }
        /* Default colours before the newline: otherwise the terminal
         * paints the rest of the line in the last background colour. */
        term_reset_colours();
        term_put("\n");
    }
    term_end();
}

