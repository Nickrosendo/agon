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

/* Marks world cell (x, y) with a white square, over what render_world
 * drew in the same area (col0, row0, cols by rows). Nothing is drawn when
 * the cell is outside the area. The next render_world redraws that
 * character cell, so a cursor that moves leaves no trace. */
void render_cursor(const Sim *s, int col0, int row0, int cols, int rows,
                   int x, int y);

/* Writes the whole world to standard output as lines of coloured
 * half-block characters, two cells per character as on screen, each line
 * ending with the default colours and '\n'. No cursor movement, no
 * screen switching: suitable for a pipe or a file. Call
 * term_detect_colour first. */
void render_snapshot(const Sim *s);

/* Forget what render_world drew last, so the next call redraws every
 * cell. Call after the screen was cleared or resized. */
void render_invalidate(void);

/* The message shown instead of the world when the window is too small. */
void render_too_small(int cols, int rows, int min_cols, int min_rows);

#endif
