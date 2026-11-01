/*
 * render: draws the world and short text lines through term. The only
 * module that knows both the world and the terminal.
 */
#ifndef AGON_RENDER_H
#define AGON_RENDER_H

#include "sim.h"

/* Draws the top left corner of the simulation's world into the screen
 * area that starts at (col0, row0) and is cols by rows characters. Each
 * character shows two cells, one above the other. If the area is larger
 * than the world, the rest is left untouched. */
void render_world(const Sim *s, int col0, int row0, int cols, int rows);

/* Forget what render_world drew last, so the next call redraws every
 * cell. Call after the screen was cleared or resized. */
void render_invalidate(void);

/* ASCII text at (col, row), cut or padded with spaces to exactly
 * max_cols characters. */
void render_text(int col, int row, const char *text, int max_cols);

/* The colours a legend can show, as used on the map. */
enum {
    RENDER_SWATCH_WATER,
    RENDER_SWATCH_ROCK,
    RENDER_SWATCH_SOIL_DRY,
    RENDER_SWATCH_SOIL_RICH,
    RENDER_SWATCH_PLANT,
    RENDER_SWATCH_WITHERED,
    RENDER_SWATCH_CARRION,
    RENDER_SWATCH_COUNT
};

/* One character cell at (col, row) filled with a RENDER_SWATCH_ colour.
 * An unknown value draws nothing. */
void render_swatch(int col, int row, int swatch);

/* How much of a creature's food is meat, from its food-chain level: 0 at
 * level 2 or below (plants only), 1 at level 3 or above (meat only). */
float render_meat_share(float level);

/* One character cell at (col, row) in the colour a creature has on the
 * map, given its inherited hue (0..1) and its meat share (0..1). */
void render_creature_swatch(int col, int row, float hue, float meat);

/* The message shown instead of the world when the window is too small. */
void render_too_small(int cols, int rows, int min_cols, int min_rows);

#endif
