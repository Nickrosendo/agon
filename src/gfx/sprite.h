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

/* The inherited traits a body shows, each 0..1. */
typedef struct {
    float size, speed, sight, plant, meat;
} BodyTraits;

enum {
    FRAME_WALK0, FRAME_WALK1, FRAME_WALK2, FRAME_WALK3,
    FRAME_REST0, FRAME_REST1,
    FRAME_BITE,
    FRAME_SPLIT,
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
 * One creature's pictures. The arrays are inside the struct, so one slot is
 * about 2 KB, and an array of them is one block with no pointers to follow.
 */
typedef struct {
    uint32_t id;
    bool ready;
    SpriteMask large[FRAME_COUNT], small[FRAME_COUNT];
} SpriteSlot;

/* One slot per slot of the creature pool. */
typedef struct {
    SpriteSlot *slots;
    int capacity;
} SpriteCache;

BodyTraits sprite_traits(const Genome *g);
/* Draws one frame into *out. size is 16 or 8. */
void sprite_build(SpriteMask *out, const BodyTraits *t, int size, int frame);
/* Where the eye is in that frame: column and row of the hole's middle. */
void sprite_eye(const BodyTraits *t, int size, int frame, int *column,
                int *row);

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

#endif
