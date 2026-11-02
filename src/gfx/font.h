/*
 * font: an embedded 8 by 8 bitmap font, each row drawn twice to make 8 by
 * 16, the shape of a terminal's character cell.
 */
#ifndef AGON_FONT_H
#define AGON_FONT_H

#include <stdint.h>

#include "canvas.h"

#define FONT_W 8
#define FONT_H 16

/* ASCII text with its top left corner at (x, y), on the background bg.
 * A byte outside printable ASCII draws as '?'. */
void font_text(Canvas *c, int x, int y, const char *ascii, uint32_t fg,
               uint32_t bg);
/* One character cell at (x, y) filled with bg, then with its bottom
 * height_eighths / 8 in fg: the eight block heights of the panel's graphs. */
void font_block(Canvas *c, int x, int y, int height_eighths, uint32_t fg,
                uint32_t bg);

#endif
