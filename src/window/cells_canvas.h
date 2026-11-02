/*
 * cells_canvas: the window program's version of cells.h: the character
 * cells end up as pixels in a canvas.
 */
#ifndef AGON_CELLS_CANVAS_H
#define AGON_CELLS_CANVAS_H

#include "canvas.h"

/* The canvas the functions of cells.h draw on from now on. It must stay
 * valid for as long as they are used. */
void cells_canvas_bind(Canvas *c);

#endif
