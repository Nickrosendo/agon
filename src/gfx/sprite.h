/*
 * sprite: creature bodies from genes. A pure function: the same traits give
 * the same pixels. Traits are rounded into a few steps first, so a small
 * mutation leaves the picture alone and close relatives look alike.
 */
#ifndef AGON_SPRITE_H
#define AGON_SPRITE_H

#include <stdbool.h>
#include <stdint.h>

#include "genome.h"

/* The inherited traits a body shows, each 0..1. The last four are parts:
 * armour shows as plates, weapon as a tusk and then a horn, legs as long legs
 * and swim as a fin tail and a fin on the back. */
typedef struct {
    float size, speed, sight, plant, meat;
    float armour, weapon, legs, swim;
} BodyTraits;

/* A large body is drawn up to this many tiles wide and tall. */
#define SPRITE_MAX_TILES 3
/* The widest picture in pixels: three tiles of 16. A row of it fits in a
 * uint64_t, which has room for 64. */
#define SPRITE_MAX_COLS (16 * SPRITE_MAX_TILES)
/* How many large bodies keep their pictures at once. Large bodies are few, so
 * a small table is enough; when it is full the one unused longest is redrawn
 * for the newcomer. */
#define SPRITE_BIG_ENTRIES 256

enum {
    FRAME_WALK0, FRAME_WALK1, FRAME_WALK2, FRAME_WALK3,
    FRAME_REST0, FRAME_REST1,
    FRAME_BITE,
    FRAME_MATE,
    FRAME_COUNT
};

/*
 * One picture, facing right. A row's column i is bit (size - 1 - i), as
 * canvas_mask reads it. body takes the family colour and white takes white;
 * a pixel is never in both. The eye is in neither: a hole.
 */
typedef struct {
    uint16_t body[16];
    uint16_t white[16];
} SpriteMask;

/*
 * One picture of a large body, size by size pixels (size up to
 * SPRITE_MAX_COLS), read as SpriteMask's rows are but 64 bits wide. outline
 * is every pixel any part touched, eye included, grown by one pixel all round:
 * drawn first in near-black, it makes the body stand out from the ground and
 * gives the eye its black.
 */
typedef struct {
    int size;
    uint64_t body[SPRITE_MAX_COLS];
    uint64_t white[SPRITE_MAX_COLS];
    uint64_t outline[SPRITE_MAX_COLS];
} WideMask;

/* One large body's pictures at one size, each frame drawn the first time it
 * is asked for. tiles 0 marks an unused entry. */
typedef struct {
    uint32_t id;
    int tiles, base;
    uint32_t last_used;
    bool ready[FRAME_COUNT];
    WideMask frames[FRAME_COUNT];
} BigEntry;

/*
 * One creature's pictures. The arrays are inside the struct, so one slot is
 * about 2 KB, and an array of them is one block with no pointers to follow.
 */
typedef struct {
    uint32_t id;
    bool ready;
    SpriteMask large[FRAME_COUNT], small[FRAME_COUNT];
} SpriteSlot;

/* One slot per slot of the creature pool, and the table for large bodies.
 * clock counts lookups in that table, to tell which entry was used longest
 * ago. */
typedef struct {
    SpriteSlot *slots;
    int capacity;
    BigEntry *big;
    uint32_t clock;
} SpriteCache;

BodyTraits sprite_traits(const Genome *g);
/* Draws one frame into *out. size is 16 or 8. */
void sprite_build(SpriteMask *out, const BodyTraits *t, int size, int frame);
/* Where the eye is in that frame: column and row of the hole's middle. */
void sprite_eye(const BodyTraits *t, int size, int frame, int *column,
                int *row);

/*
 * How many tiles wide a body of this size is drawn: 1 below SPRITE_TWO_TILES,
 * 2 below SPRITE_THREE_TILES, then 3, however large. Only drawing reads it: in
 * the simulation every creature takes one cell. A size that is not a number
 * gives 1.
 */
#define SPRITE_TWO_TILES 3.0f
#define SPRITE_THREE_TILES 6.0f
int sprite_tiles_for(float size);
/* Draws one frame of a picture size pixels across into *out, for a body
 * drawn several tiles wide: size is base * tiles, at most SPRITE_MAX_COLS. */
void sprite_build_wide(WideMask *out, const BodyTraits *t, int size, int frame);
/*
 * Scars: up to count short cuts (at most 3) across the body, as rows of a
 * size-pixel picture, only where body has pixels. Placed by the body's own
 * extent, so they land on it whatever its shape. No body, no scars.
 */
void sprite_scars(uint64_t *out, const uint64_t *body, int size, int count);

/* False when memory runs out; nothing to free then. */
bool sprite_cache_init(SpriteCache *c, int capacity);
/* Frees the slots and empties *c; safe to call twice. */
void sprite_cache_free(SpriteCache *c);
/* Forgets every picture. Needed after r or a load, when ids start again and
 * a slot could hold a different creature with the same id as before. */
void sprite_cache_reset(SpriteCache *c);
/* The picture for the creature with this id in this pool slot, drawn the
 * first time it is asked for and redrawn when the slot's id changes. NULL
 * for a slot outside the cache. size is 16 or 8. */
const SpriteMask *sprite_get(SpriteCache *c, int slot, uint32_t id,
                             const Genome *g, int size, int frame);
/* The picture of a large body: tiles of 2 or more, base 16 or 8 (the size of
 * one tile in pixels). NULL for any other tiles or base. The pointer stays
 * good until the next call, which may reuse its entry. */
const WideMask *sprite_get_wide(SpriteCache *c, uint32_t id, const Genome *g,
                                int tiles, int base, int frame);

#endif
