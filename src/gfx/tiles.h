/*
 * tiles: the symbols of the terrain, plants and carrion, as one-colour
 * masks. Colours are chosen by scene, not here.
 */
#ifndef AGON_TILES_H
#define AGON_TILES_H

#include <stdint.h>

/* The last two are not terrain but marks drawn over a bitten cell: the flash
 * of a fight, and crumbs of food flying off. */
enum { TILE_WATER, TILE_ROCK, TILE_SOIL, TILE_PLANT, TILE_CARRION, TILE_SLASH,
       TILE_CRUMBS, TILE_KINDS };
#define TILE_VARIANTS 4
#define TILE_WATER_FRAMES 4
/* How many pictures each of the two marks cycles through. */
#define TILE_MARK_FRAMES 4
/* How many dot densities soil has. */
#define TILE_SOIL_DETAILS 4

/*
 * Draws one symbol into rows, as canvas_mask reads it. size is 16 or 8;
 * rows beyond size are set to 0. variant picks one of TILE_VARIANTS forms.
 * detail: for TILE_PLANT the density 0..VEG_MAX, for TILE_SOIL the dot
 * density 0..3, otherwise ignored. frame matters for water and the marks.
 */
void tiles_build(uint16_t rows[16], int kind, int variant, int detail, int size,
                 int frame);

/* Which variant a cell uses: a hash of its position, so it never changes,
 * and never touches the simulation's random numbers. */
int tiles_variant(int x, int y);

#endif
