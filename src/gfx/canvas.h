/*
 * canvas: a block of 32-bit pixels and the few ways of drawing into it.
 * Knows nothing about the simulation, the terminal or SDL. Every drawing
 * function clips: whatever falls outside the block is simply not drawn.
 */
#ifndef AGON_CANVAS_H
#define AGON_CANVAS_H

#include <stdbool.h>
#include <stdint.h>

/*
 * px holds w * h pixels, row after row, each 0xFFRRGGBB. capacity is how
 * many pixels the allocated block can hold, which may be more than w * h
 * after the canvas shrank. An empty canvas (w or h of 0) has px NULL or an
 * unused block, and every drawing function does nothing on it.
 */
typedef struct {
    uint32_t *px;
    int w, h, capacity;
} Canvas;

uint32_t canvas_rgb(uint8_t r, uint8_t g, uint8_t b);

/* Allocates a w by h canvas, all black. A w or h of 0 is allowed, and is
 * an empty canvas. False when memory runs out; nothing to free then. */
bool canvas_init(Canvas *c, int w, int h);
/* Changes the size. The block is reallocated only when the new size needs
 * more than capacity, so shrinking and growing back costs nothing. The
 * pixels' contents are not kept. False when memory runs out; the canvas
 * then keeps its old size and block. */
bool canvas_resize(Canvas *c, int w, int h);
/* Frees the block and empties *c; safe to call twice. */
void canvas_free(Canvas *c);

void canvas_fill(Canvas *c, uint32_t colour);
void canvas_rect(Canvas *c, int x, int y, int w, int h, uint32_t colour);
/*
 * Draws the set bits of rows[0 .. size - 1], with the mask's top left
 * corner at (x, y), each bit as a scale by scale square. Column i of a row
 * is bit (size - 1 - i), so a row reads left to right as binary. flip draws
 * the mirror image, left to right.
 */
void canvas_mask(Canvas *c, const uint16_t *rows, int size, int x, int y,
                 int scale, bool flip, uint32_t colour);
/* A rectangle drawing may not leave, in canvas pixels. */
typedef struct {
    int x, y, w, h;
} CanvasClip;

/* canvas_rect, kept inside clip as well as inside the canvas. */
void canvas_rect_clip(Canvas *c, int x, int y, int w, int h, uint32_t colour,
                      CanvasClip clip);
/* canvas_mask for rows up to 64 pixels wide, kept inside clip. Each row's set
 * bits are drawn as runs, one rectangle per run of neighbouring pixels. */
void canvas_mask_wide(Canvas *c, const uint64_t *rows, int size, int x, int y,
                      int scale, bool flip, uint32_t colour, CanvasClip clip);
/* Draws a w by h block of ink indices, row after row, stride apart; index 0 is
 * not drawn, any other is colours[index]. flip mirrors it left to right. */
void canvas_picture(Canvas *c, const uint8_t *ink, int stride, int w, int h,
                    int x, int y, int scale, bool flip, const uint32_t *colours,
                    CanvasClip clip);
/* Multiplies every pixel inside the ellipse by keep / 256: a soft shadow. */
void canvas_darken_ellipse(Canvas *c, int cx, int cy, int rx, int ry, int keep,
                           CanvasClip clip);
void canvas_pixel(Canvas *c, int x, int y, int scale, uint32_t colour,
                  CanvasClip clip);
/* Copies all of from onto c with from's corner at (x, y), clipped. */
void canvas_copy(Canvas *c, const Canvas *from, int x, int y);
/* Writes the canvas as a PPM image, the simplest picture format there is:
 * a short text header, then three bytes per pixel. False if the file could
 * not be written. */
bool canvas_write_ppm(const Canvas *c, const char *path);

#endif
