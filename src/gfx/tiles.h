/*
 * tiles: the pictures of the ground, plants, carrion and marks. The ground is
 * built as autotiles: each cell's picture depends on which kind of ground its
 * eight neighbours are, so edges between kinds come out ragged, with earthen
 * banks above the water. Colours are chosen by scene, not here: a tile holds,
 * for each pixel, which cell it shows, how light it is and what edge it is on.
 */
#ifndef AGON_TILES_H
#define AGON_TILES_H

#include <stdbool.h>
#include <stdint.h>

#include "world.h" /* World */

/* The one-colour masks still drawn the old way. TILE_PLANT goes in task 4,
 * TILE_SLASH in task 12, TILE_CRUMBS in task 13. The last two are not ground
 * but marks drawn over a bitten cell: the flash of a fight, and crumbs of food
 * flying off. */
enum { TILE_PLANT, TILE_SLASH, TILE_CRUMBS, TILE_RAIN, TILE_KINDS };
#define TILE_VARIANTS 4
/* Rain is redrawn with the ground layer's water clock. */
#define TILE_RAIN_FRAMES 4
/* How many pictures each of the two marks cycles through. */
#define TILE_MARK_FRAMES 4

/*
 * Draws one symbol into rows, as canvas_mask reads it. size is 16 or 8;
 * rows beyond size are set to 0. variant picks one of TILE_VARIANTS forms.
 * detail: for TILE_PLANT the density 0..VEG_MAX, otherwise ignored. frame
 * matters for rain and the marks.
 */
void tiles_build(uint16_t rows[16], int kind, int variant, int detail, int size,
                 int frame);

/* Which variant a cell uses: a hash of its position, so it never changes,
 * and never touches the simulation's random numbers. */
int tiles_variant(int x, int y);

/* ---- The ground as autotiles ---- */

/* The largest tile built: Close draws 16 at scale 2. */
#define TILE_MAX 16
#define TILE_GROUND_VARIANTS 8
/* The four kinds of ground a tile tells apart. */
enum { CLASS_WATER, CLASS_SHALLOW, CLASS_SOIL, CLASS_ROCK };
/* A TERRAIN_ value to a CLASS_ value. */
int tiles_class(int terrain);
/* 0..TILE_GROUND_VARIANTS - 1, a hash of the position. */
int tiles_ground_variant(int x, int y);
/*
 * What a cell's tile is built from: its ground variant in bits 0..2, then the
 * classes of the 3 by 3 cells around it (wrapped round the world's edges),
 * 2 bits each in reading order, the cell itself in the middle (bits 11..12).
 */
uint32_t tiles_key(const World *w, int x, int y);
/*
 * How far a pixel of a tile looks for its ground: the ragged edge. A function
 * of its place (lx, ly) in the tile only, so neighbouring tiles agree on where
 * their edge lies.
 */
void tiles_jitter(int size, int lx, int ly, int *jx, int *jy);

/* One pixel's code: which of the 9 cells it shows (0..8 in reading order, 4 is
 * the cell itself), its shade (0 dark, 1 middle, 2 light) and its edge. */
#define TILE_SOURCE(code) ((code) & 15u)
#define TILE_SHADE(code) (((code) >> 4) & 3u)
#define TILE_EDGE(code) (((code) >> 6) & 15u)
/* Water touching land: foam comes and goes here. */
#define TILE_FOAM 0x400u
enum { EDGE_NONE, EDGE_BANK, EDGE_BANK_DARK, EDGE_SHADOW, EDGE_ROCK_LIT,
       EDGE_ROCK_SPECK, EDGE_GRASS_SPECK };
/* The class of a key's cell, source 0..8 as TILE_SOURCE gives it. */
int tiles_key_class(uint32_t key, int source);
/* Fills code[0 .. size * size - 1], row after row, for a tile of this key.
 * size is 16 or 8. */
void tiles_build_auto(uint16_t *code, uint32_t key, int size);

/*
 * Tiles are built the first time their key is seen, and kept: a fixed table of
 * entries, the one unused longest replaced when it is full. Finding a key goes
 * through a chained hash: head[b] is the first entry of bucket b, next[e] the
 * entry after e in its bucket, -1 ending a chain. Indexes, not pointers, so the
 * lists stay valid however the table is laid out.
 */
typedef struct {
    uint32_t key;
    int size;
    uint32_t last_used; /* the cache's clock when it was last asked for */
    bool used, has_foam;
    uint16_t code[TILE_MAX * TILE_MAX];
} TileEntry;

typedef struct {
    TileEntry *entries;
    int capacity;
    int32_t *head, *next;
    uint32_t clock;
    long builds; /* how many tiles were built since init: for tests and measuring */
} TileCache;

/* False when memory runs out; nothing to free then. The scene uses 2048. */
bool tiles_cache_init(TileCache *c, int entries);
/* Frees the table and empties *c; safe to call twice. */
void tiles_cache_free(TileCache *c);
/* Forgets every tile (on a zoom change and scene_reset). builds keeps counting. */
void tiles_cache_reset(TileCache *c);
/* The tile of this key at this size, built now if it is not kept. NULL only on
 * a cache that failed to initialise. */
const TileEntry *tiles_cache_get(TileCache *c, uint32_t key, int size);

/*
 * Carrion: bones on a dried stain. Fills ink[0 .. size * size - 1], row after
 * row (and sets the rest of the TILE_MAX * TILE_MAX to 0), each 0 for nothing,
 * 1 bone light, 2 bone dark, 3 stain. variant is 0..TILE_VARIANTS - 1.
 */
void tiles_build_bones(uint8_t ink[TILE_MAX * TILE_MAX], int variant, int size);

#endif
