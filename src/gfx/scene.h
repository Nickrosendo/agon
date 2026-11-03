/*
 * scene: draws one frame of the world into a canvas, from the simulation,
 * the camera, the sprites and tiles and the animation memory. Reads the
 * simulation only: no writes through the Sim, and none of its random numbers.
 */
#ifndef AGON_SCENE_H
#define AGON_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "anim.h"
#include "camera.h"
#include "canvas.h"
#include "family.h"
#include "font.h"
#include "inspect.h"
#include "panel.h"
#include "sim.h"
#include "sprite.h"
#include "tiles.h"

/* Index 0 of each table's first dimension is the 16-pixel size, 1 the
 * 8-pixel size. These are arrays of arrays, all one block inside the Scene:
 * water[s][v][f] is a row table of 16 numbers, with no pointers to follow. */
typedef struct {
    SpriteCache sprites;
    Anim anim;
    uint16_t water[2][TILE_VARIANTS][TILE_WATER_FRAMES][16];
    uint16_t rock[2][TILE_VARIANTS][16];
    uint16_t soil[2][TILE_VARIANTS][TILE_SOIL_DETAILS][16];
    uint16_t plant[2][TILE_VARIANTS][VEG_MAX + 1][16];
    uint16_t carrion[2][TILE_VARIANTS][16];
    uint16_t slash[2][TILE_MARK_FRAMES][16];
    uint16_t crumbs[2][TILE_MARK_FRAMES][16];
    /* The ground as last drawn, and what it was drawn from. Most frames fall
     * between two ticks and have the same ground, so they only copy it. */
    Canvas ground;
    bool ground_valid;
    uint64_t ground_tick;
    int ground_water, ground_level;
    float ground_cx, ground_cy;
} Scene;

/* How the window is divided, in pixels. */
#define SCENE_TEXT_H FONT_H
#define SCENE_PANEL_W (PANEL_COLS * FONT_W)
/* Below this the window shows only a message. */
#define SCENE_MIN_COLS 20
#define SCENE_MIN_ROWS 6
#define SCENE_MIN_W (SCENE_MIN_COLS * FONT_W)
#define SCENE_MIN_H (SCENE_MIN_ROWS * FONT_H)
/* An enlarged body: 16 pixels, each four screen pixels wide. */
#define SCENE_BODY_BIG 64
/* A panel keeps at least this many text rows before the bodies get any: enough
 * for all of its lines but the family colours. */
#define SCENE_PANEL_MIN_ROWS 19
/* Rows for: one of caption, four of enlarged body. And for the strip: one of
 * bodies and one of caption above them. */
#define SCENE_ENLARGED_ROWS 5
#define SCENE_STRIP_ROWS 2

/* The title line on top, the help line below, the world between them and the
 * panel on the right. When the window is narrow the panel goes first. */
typedef struct {
    bool too_small;
    bool has_panel;
    int view_x, view_y, view_w, view_h;
    int panel_x, panel_y, panel_rows;
    /* Below the panel's text, family_rows rows from pixel family_y. */
    bool show_enlarged, show_strip;
    int family_y, family_rows;
    int help_y;
} Layout;

void scene_layout(Layout *l, int window_w, int window_h);

/* False when memory runs out; nothing to free then. */
bool scene_init(Scene *sc, const Sim *s);
void scene_free(Scene *sc);
/* After r: ids start again, so everything remembered is wrong. */
void scene_reset(Scene *sc);
/* Which picture the water shows at this time. While paused it is the only thing
 * that moves, so a frame where it has not changed need not be drawn. */
int scene_water_frame(uint64_t water_ms);
/* Once per frame, after the frame's ticks. */
void scene_update(Scene *sc, const Sim *s, int ticks_per_second);
/*
 * Draws the camera's view with its top left corner at (view_x, view_y).
 * tick_fraction (0..1) is how far the frame is towards the next tick.
 * water_ms drives the water and keeps running while paused; body_ms drives
 * the creatures' breathing, and stops while paused.
 */
void scene_draw_world(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                      float tick_fraction, uint64_t water_ms, uint64_t body_ms,
                      int view_x, int view_y);

/* The followed creature four times its size, in its live animation frame,
 * with its top left corner at (x, y). */
void scene_draw_followed(Scene *sc, Canvas *c, const Sim *s,
                         const Creature *followed, uint64_t now_ms, int x, int y);
/* A family's average body at the same size, breathing. */
void scene_draw_sample(Canvas *c, const FamilySample *sample, uint64_t now_ms,
                       int x, int y);
/* A family's bodies over time, oldest on the left, as many as fit in w pixels.
 * Nothing when the family has no record. */
void scene_draw_strip(Canvas *c, const Families *f, uint32_t lineage, int x,
                      int y, int w);
/* A click at world point (wx, wy): follows the nearest creature within two
 * cells, going round the world's edges, or lets go when there is none. */
void scene_click(Inspect *in, const Sim *s, float wx, float wy);
/* Once per frame, after scene_update. Moves the camera to the followed creature
 * as drawn, between cells if it is gliding. Returns that creature, or NULL when
 * nobody is followed: if it has just died, the camera stays where it was. */
const Creature *scene_follow(const Scene *sc, Camera *cam, Inspect *in,
                             const Sim *s, float tick_fraction);
/* Corner marks around the followed creature, over the world. */
void scene_draw_cursor(const Scene *sc, Canvas *c, const Camera *cam,
                       const Sim *s, const Creature *followed,
                       float tick_fraction, int view_x, int view_y);

#endif
