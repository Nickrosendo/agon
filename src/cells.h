/*
 * cells: text and coloured squares on a grid of character cells. The panel
 * draws through these and does not know where they end up. Each program
 * links exactly one version: the terminal's (cells_term.c) sends them to
 * the terminal; the window's paints them into its pixels. Which one is
 * chosen by the Makefile, at link time.
 */
#ifndef AGON_CELLS_H
#define AGON_CELLS_H

/* ASCII text at (col, row), cut or padded with spaces to exactly
 * max_cols characters. */
void render_text(int col, int row, const char *text, int max_cols);

/* UTF-8 text at (col, row) in the text colours, written as it is: no
 * cutting or padding, since its width on screen is not its length in
 * bytes. The caller makes sure it fits. */
void render_utf8(int col, int row, const char *text);

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

/* One character cell at (col, row) in the colour a creature has on the
 * map, given its inherited hue (0..1) and its meat share (0..1). */
void render_creature_swatch(int col, int row, float hue, float meat);

#endif
