#include "camera.h"

#include <math.h> /* floorf, fmodf */

/* The screen pixels per cell of the three fixed levels. */
static const int FIXED_PPC[] = {32, 16, 8};

/* A value wrapped into 0 .. size. fmodf keeps the sign of its first
 * argument, unlike a mathematician's modulo: fmodf(-1, 256) is -1, not 255.
 * C's % on integers does the same. */
static float wrap(float v, int size)
{
    float m = fmodf(v, (float)size);
    if (m < 0.0f) {
        m += (float)size;
    }
    /* A tiny negative m plus size can round up to exactly size. */
    return m >= (float)size ? 0.0f : m;
}

/* The shortest signed distance from a to b round a wrapped axis. */
static float shortest(float a, float b, int size)
{
    float half = (float)size / 2.0f;
    return wrap(b - a + half, size) - half;
}

/* Whether a whole axis fits in the view at this many pixels per cell. World
 * sides are at most 4096 and ppc at most 32, so the product stays far below
 * an int's limit. */
static bool axis_fits(int world, int view, int ppc)
{
    return world * ppc <= view;
}

int camera_ppc(const Camera *c)
{
    if (c->level != CAMERA_WORLD) {
        return FIXED_PPC[c->level];
    }
    for (int ppc = 4; ppc > 1; ppc /= 2) {
        if (axis_fits(c->world_w, c->view_w, ppc) &&
            axis_fits(c->world_h, c->view_h, ppc)) {
            return ppc;
        }
    }
    return 1;
}

int camera_sprite_size(const Camera *c)
{
    switch (c->level) {
    case CAMERA_CLOSE:
    case CAMERA_NORMAL:
        return 16;
    case CAMERA_FAR:
        return 8;
    default:
        return 0;
    }
}

int camera_sprite_scale(const Camera *c)
{
    return c->level == CAMERA_CLOSE ? 2 : 1;
}

void camera_init(Camera *c, int world_w, int world_h, int view_w, int view_h)
{
    c->level = CAMERA_NORMAL;
    c->world_w = world_w;
    c->world_h = world_h;
    c->cx = (float)world_w / 2.0f;
    c->cy = (float)world_h / 2.0f;
    camera_resize(c, view_w, view_h);
}

void camera_resize(Camera *c, int view_w, int view_h)
{
    c->view_w = view_w < 0 ? 0 : view_w;
    c->view_h = view_h < 0 ? 0 : view_h;
}

bool camera_world_fits(const Camera *c)
{
    int ppc = camera_ppc(c);
    return axis_fits(c->world_w, c->view_w, ppc) &&
           axis_fits(c->world_h, c->view_h, ppc);
}

/* One axis of camera_to_screen. */
static int axis_to_screen(float world_pos, float centre, int world, int view,
                          int ppc)
{
    if (axis_fits(world, view, ppc)) {
        /* Whole numbers first, so cells line up exactly with the grid. */
        int margin = (view - world * ppc) / 2;
        return margin + (int)floorf(wrap(world_pos, world) * (float)ppc);
    }
    float d = shortest(centre, world_pos, world);
    /* floorf, not a plain cast: (int)-0.5f is 0 (a cast drops the fraction,
     * towards zero), which would squash two pixels into one left of the
     * middle. */
    return view / 2 + (int)floorf(d * (float)ppc);
}

void camera_to_screen(const Camera *c, float wx, float wy, int *sx, int *sy)
{
    int ppc = camera_ppc(c);
    *sx = axis_to_screen(wx, c->cx, c->world_w, c->view_w, ppc);
    *sy = axis_to_screen(wy, c->cy, c->world_h, c->view_h, ppc);
}

/* One axis of camera_to_world. + 0.5f aims at the pixel's middle, so going
 * back with camera_to_screen lands on the same pixel despite rounding. */
static float axis_to_world(int screen, float centre, int world, int view,
                           int ppc)
{
    float pixel = (float)screen + 0.5f;
    if (axis_fits(world, view, ppc)) {
        int margin = (view - world * ppc) / 2;
        return wrap((pixel - (float)margin) / (float)ppc, world);
    }
    return wrap(centre + (pixel - (float)(view / 2)) / (float)ppc, world);
}

void camera_to_world(const Camera *c, int sx, int sy, float *wx, float *wy)
{
    int ppc = camera_ppc(c);
    *wx = axis_to_world(sx, c->cx, c->world_w, c->view_w, ppc);
    *wy = axis_to_world(sy, c->cy, c->world_h, c->view_h, ppc);
}

void camera_centre_on(Camera *c, float world_x, float world_y)
{
    c->cx = wrap(world_x, c->world_w);
    c->cy = wrap(world_y, c->world_h);
}

void camera_pan(Camera *c, int dx_pixels, int dy_pixels)
{
    float ppc = (float)camera_ppc(c);
    camera_centre_on(c, c->cx + (float)dx_pixels / ppc,
                     c->cy + (float)dy_pixels / ppc);
}

/* The centre that puts world_pos under the screen pixel on this axis. */
static float centre_for(float world_pos, int screen, int view, int ppc)
{
    float pixel = (float)screen + 0.5f;
    return world_pos - (pixel - (float)(view / 2)) / (float)ppc;
}

void camera_zoom(Camera *c, int steps, int mouse_x, int mouse_y)
{
    float wx, wy;
    camera_to_world(c, mouse_x, mouse_y, &wx, &wy);
    int level = c->level - steps; /* closer is a lower level number */
    level = level < 0 ? 0 : level;
    c->level = level > CAMERA_WORLD ? CAMERA_WORLD : level;
    int ppc = camera_ppc(c);
    camera_centre_on(c, centre_for(wx, mouse_x, c->view_w, ppc),
                     centre_for(wy, mouse_y, c->view_h, ppc));
}

/* One axis of camera_cells. */
static void axis_cells(float centre, int world, int view, int ppc, int *first,
                       int *count, int *screen)
{
    if (axis_fits(world, view, ppc)) {
        *first = 0;
        *count = view > 0 ? world : 0;
        *screen = (view - world * ppc) / 2;
        return;
    }
    /* The cell at the view's left edge, possibly off the world's side. */
    float left = centre - (float)(view / 2) / (float)ppc;
    int cell = (int)floorf(left);
    *screen = view / 2 + (int)floorf(((float)cell - centre) * (float)ppc);
    *first = (int)wrap((float)cell, world);
    /* Rounded up, so a part cell at the far edge is drawn too. *screen is
     * zero or negative here, which already counts the near edge's part cell. */
    *count = view > 0 ? (view - *screen + ppc - 1) / ppc : 0;
}

void camera_cells(const Camera *c, CameraCells *out)
{
    int ppc = camera_ppc(c);
    axis_cells(c->cx, c->world_w, c->view_w, ppc, &out->first_x, &out->cols,
               &out->screen_x);
    axis_cells(c->cy, c->world_h, c->view_h, ppc, &out->first_y, &out->rows,
               &out->screen_y);
}

bool camera_on_world(const Camera *c, int sx, int sy)
{
    CameraCells v;
    camera_cells(c, &v);
    int ppc = camera_ppc(c);
    return sx >= v.screen_x && sx < v.screen_x + v.cols * ppc &&
           sy >= v.screen_y && sy < v.screen_y + v.rows * ppc;
}
