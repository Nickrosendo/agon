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

/* The message shown instead of the world when the window is too small. */
void render_too_small(int cols, int rows, int min_cols, int min_rows);

#endif
