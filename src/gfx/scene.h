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
} Scene;

/* False when memory runs out; nothing to free then. */
bool scene_init(Scene *sc, const Sim *s);
void scene_free(Scene *sc);
/* After r: ids start again, so everything remembered is wrong. */
void scene_reset(Scene *sc);
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

#endif
