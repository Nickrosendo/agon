/*
 * camera: which part of the world the view shows, and at what size. Pure
 * maths: no drawing, no simulation. The world wraps at its edges, so the
 * camera does too: all distances go the shortest way round. Along an axis
 * where the whole world fits in the view, it is centred instead, with
 * nothing repeated.
 */
#ifndef AGON_CAMERA_H
#define AGON_CAMERA_H

#include <stdbool.h>

/* The zoom steps, closest first. */
enum { CAMERA_CLOSE, CAMERA_NORMAL, CAMERA_FAR, CAMERA_WORLD, CAMERA_LEVELS };

/* cx, cy: the world point at the middle of the view, in cells, kept inside
 * the world. */
typedef struct {
    int level;
    float cx, cy;
    int view_w, view_h;   /* screen pixels; 0 is allowed */
    int world_w, world_h; /* cells */
} Camera;

/* What the view covers along both axes, for drawing cell by cell. */
typedef struct {
    int first_x, first_y; /* the first visible cell, inside the world */
    int cols, rows;       /* how many cells to draw from there, wrapping */
    int screen_x, screen_y; /* where the first cell's corner lands */
} CameraCells;

/* At CAMERA_NORMAL, centred on the world. */
void camera_init(Camera *c, int world_w, int world_h, int view_w, int view_h);
void camera_resize(Camera *c, int view_w, int view_h);
/* Screen pixels per cell: 32, 16, 8, or at CAMERA_WORLD the largest of 4, 2
 * and 1 at which the world fits the view (1 when none does). */
int camera_ppc(const Camera *c);
/* The symbol size at this level: 16, 16, 8, or 0 for a coloured square. */
int camera_sprite_size(const Camera *c);
/* How many screen pixels each symbol pixel takes: 2, 1, 1, 1. */
int camera_sprite_scale(const Camera *c);
/* Moves steps levels closer (negative: further away), stopping at the ends,
 * keeping the world point under the mouse where it is on screen. */
void camera_zoom(Camera *c, int steps, int mouse_x, int mouse_y);
/* Moves the view by this many screen pixels. */
void camera_pan(Camera *c, int dx_pixels, int dy_pixels);
void camera_centre_on(Camera *c, float world_x, float world_y);
/* Where a world point lands on screen, using its nearest wrapped image. */
void camera_to_screen(const Camera *c, float wx, float wy, int *sx, int *sy);
/* The world point at a screen pixel's middle, wrapped into the world. */
void camera_to_world(const Camera *c, int sx, int sy, float *wx, float *wy);
/* False for a screen pixel on the black around a world smaller than the view. */
bool camera_on_world(const Camera *c, int sx, int sy);
/* True when the whole world is visible: centred on black, no repeats. */
bool camera_world_fits(const Camera *c);
/* The cells the view covers. */
void camera_cells(const Camera *c, CameraCells *out);

#endif
