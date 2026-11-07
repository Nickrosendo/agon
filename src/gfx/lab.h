/*
 * lab: the species lab, a page of the window (the b key) that shows every
 * species in rows by role, each standing on a strip of ground with its name,
 * drawn from the detailed pictures. It only looks: it reads the picture cache
 * and writes pixels, and touches nothing of the simulation.
 */
#ifndef AGON_LAB_H
#define AGON_LAB_H

#include <stdint.h>

#include "canvas.h"
#include "sprite.h"

/* Species per row: the most any role has. */
#define LAB_COLUMNS 3
/* The number keys 1 to LAB_KEYS choose what the animals do. */
#define LAB_KEYS 6
/* A ground strip's bottom: the line and the six rows of ground under it, in
 * art pixels. */
#define LAB_GROUND_ROWS 7

/* The ACTION_ value number key `number` (1 to LAB_KEYS: walk, run, rest,
 * graze, bite, sleep) chooses; -1 for any other number. */
int lab_action_of_key(int number);

/* The species shown in row `role` (a ROLE_ value; rows run hunters, swimmers,
 * armoured, runners, watchers, plant-eaters) at this column, in table order; -1
 * for an empty cell or a place outside the page. */
int lab_species_at(int role, int column);

/* Every species in rows by role, each on a strip of ground, from the detailed
 * pictures (SPRITE_BASE_DETAIL, the mockup lab's scale) at scale 2, or scale 1
 * when the area is too small for two. action is an ACTION_ value; body_ms the
 * window's body clock, which picks each animal's frame of the action (each at
 * its own pace and out of step) and its blinks. Draws only inside
 * the area (x, y, w, h); an area too small for the page is just filled. */
void lab_draw(SpriteCache *sprites, Canvas *c, int action, uint64_t body_ms,
              int x, int y, int w, int h);

/* The sky's colour above a ground strip, as a canvas pixel. */
uint32_t lab_sky(void);
/* A strip of the mockup's lab ground in the box (x, y, w, h): sky, then a
 * line and ground of two greens by hash in its bottom LAB_GROUND_ROWS art
 * pixels of `scale` screen pixels each. salt keeps strips from repeating one
 * another. Drawn only inside clip. Returns the line's canvas row: a picture's
 * ground point goes one art pixel below it. The panel uses it too. */
int lab_draw_ground(Canvas *c, int x, int y, int w, int h, int scale, int salt,
                    CanvasClip clip);

/* Where one cell's strip is for an area of w by h at the origin: its box in
 * (bx, by, bw, bh), and the scale the page is drawn at (0, with the box
 * untouched, when the area is too small for the page). The tests read it. */
int lab_cell_box(SpriteCache *sprites, int action, int w, int h, int role, int column,
                 int *bx, int *by, int *bw, int *bh);

#endif
