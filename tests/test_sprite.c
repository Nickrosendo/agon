#include "test.h"

#include <math.h>   /* INFINITY, NAN */
#include <stdlib.h> /* calloc, free */
#include <string.h> /* memcmp */

#include "sprite.h"

/* How many bits are set in a 16-bit row. */
static int bits(unsigned row)
{
    int n = 0;
    for (; row != 0; row >>= 1) {
        n += (int)(row & 1u);
    }
    return n;
}

static int mask_bits(const uint16_t *rows)
{
    int n = 0;
    for (int j = 0; j < 16; j++) {
        n += bits(rows[j]);
    }
    return n;
}

static BodyTraits traits(float size, float speed, float sight, float plant,
                         float meat)
{
    /* Named fields: any left out are zero, here the four parts. A plain
     * {a, b, c, d, e} would do the same, but -Wextra warns that it may be a
     * mistake. */
    BodyTraits t = {.size = size, .speed = speed, .sight = sight,
                    .plant = plant, .meat = meat};
    return t;
}

static void test_same_traits_same_pixels(void)
{
    BodyTraits t = traits(0.6f, 0.5f, 0.4f, 0.8f, 0.1f);
    SpriteMask a, b;
    sprite_build(&a, &t, 16, FRAME_WALK1);
    sprite_build(&b, &t, 16, FRAME_WALK1);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

static void test_bigger_size_more_body_pixels(void)
{
    BodyTraits small = traits(0.0f, 0.5f, 0.3f, 0.0f, 0.0f);
    BodyTraits large = traits(1.0f, 0.5f, 0.3f, 0.0f, 0.0f);
    SpriteMask a, b;
    sprite_build(&a, &small, 16, FRAME_REST0);
    sprite_build(&b, &large, 16, FRAME_REST0);
    CHECK(mask_bits(b.body) > mask_bits(a.body));
}

static void test_speed_sets_leg_count(void)
{
    const float speeds[3] = {0.1f, 0.5f, 0.9f};
    const int legs[3] = {2, 4, 6};
    /* The bottom row holds nothing but feet, at every body size. */
    for (int s = 0; s < 4; s++) {
        for (int k = 0; k < 3; k++) {
            BodyTraits t = traits((float)s / 3.0f, speeds[k], 0.3f, 0.0f, 0.0f);
            SpriteMask m;
            sprite_build(&m, &t, 16, FRAME_WALK0);
            CHECK(bits(m.body[15]) == legs[k]);
        }
    }
}

static void test_meat_gives_teeth_plant_gives_spots(void)
{
    SpriteMask m;
    BodyTraits neither = traits(0.5f, 0.5f, 0.3f, 0.0f, 0.0f);
    sprite_build(&m, &neither, 16, FRAME_REST0);
    CHECK(mask_bits(m.white) == 0);
    for (int s = 0; s < 4; s++) {
        BodyTraits meat = traits((float)s / 3.0f, 0.5f, 0.3f, 0.0f, 1.0f);
        sprite_build(&m, &meat, 16, FRAME_REST0);
        CHECK(mask_bits(m.white) > 0);
        BodyTraits plant = traits((float)s / 3.0f, 0.5f, 0.3f, 1.0f, 0.0f);
        sprite_build(&m, &plant, 16, FRAME_REST0);
        CHECK(mask_bits(m.white) > 0);
    }
}

static void test_body_and_white_never_overlap(void)
{
    BodyTraits t = traits(0.9f, 0.9f, 0.9f, 0.9f, 0.9f);
    for (int f = 0; f < FRAME_COUNT; f++) {
        SpriteMask m;
        sprite_build(&m, &t, 16, f);
        for (int j = 0; j < 16; j++) {
            CHECK((m.body[j] & m.white[j]) == 0);
        }
    }
}

static void test_sight_changes_eye(void)
{
    BodyTraits low = traits(0.5f, 0.5f, 0.0f, 0.0f, 0.0f);
    BodyTraits high = traits(0.5f, 0.5f, 1.0f, 0.0f, 0.0f);
    SpriteMask a, b;
    sprite_build(&a, &low, 16, FRAME_REST0);
    sprite_build(&b, &high, 16, FRAME_REST0);
    CHECK(memcmp(&a, &b, sizeof(a)) != 0);
    /* The eye's pixel is a hole, and it has body around it to be seen by. */
    const BodyTraits *both[2] = {&low, &high};
    const SpriteMask *masks[2] = {&a, &b};
    for (int k = 0; k < 2; k++) {
        int column, row;
        sprite_eye(both[k], 16, FRAME_REST0, &column, &row);
        CHECK(column >= 0 && column < 16 && row >= 1 && row < 15);
        unsigned bit = 1u << (15 - column);
        CHECK((masks[k]->body[row] & bit) == 0);
        CHECK((masks[k]->white[row] & bit) == 0);
        CHECK(bits(masks[k]->body[row - 1]) + bits(masks[k]->body[row + 1]) > 0);
    }
}

static void test_small_mutation_same_picture(void)
{
    BodyTraits a = traits(0.40f, 0.5f, 0.3f, 0.8f, 0.1f);
    BodyTraits b = traits(0.41f, 0.5f, 0.3f, 0.8f, 0.1f);
    for (int f = 0; f < FRAME_COUNT; f++) {
        SpriteMask ma, mb;
        sprite_build(&ma, &a, 16, f);
        sprite_build(&mb, &b, 16, f);
        CHECK(memcmp(&ma, &mb, sizeof(ma)) == 0);
    }
}

static void test_walk_frames_differ(void)
{
    BodyTraits t = traits(0.5f, 0.5f, 0.3f, 0.0f, 0.0f);
    SpriteMask m[FRAME_COUNT];
    for (int f = 0; f < FRAME_COUNT; f++) {
        sprite_build(&m[f], &t, 16, f);
    }
    CHECK(memcmp(&m[FRAME_WALK0], &m[FRAME_WALK1], sizeof(m[0])) != 0);
    CHECK(memcmp(&m[FRAME_WALK1], &m[FRAME_WALK3], sizeof(m[0])) != 0);
    CHECK(memcmp(&m[FRAME_REST0], &m[FRAME_REST1], sizeof(m[0])) != 0);
    CHECK(memcmp(&m[FRAME_REST0], &m[FRAME_BITE], sizeof(m[0])) != 0);
    CHECK(memcmp(&m[FRAME_REST0], &m[FRAME_MATE], sizeof(m[0])) != 0);
}

static void test_every_frame_stays_in_tile(void)
{
    /* At size 8: no bits above bit 7, no rows from 8 up. */
    for (int s = 0; s <= 4; s++) {
        for (int v = 0; v <= 4; v++) {
            for (int e = 0; e <= 4; e++) {
                BodyTraits t = traits((float)s / 4.0f, (float)v / 4.0f,
                                      (float)e / 4.0f, (float)(s % 2),
                                      (float)(v % 2));
                for (int f = 0; f < FRAME_COUNT; f++) {
                    SpriteMask m;
                    sprite_build(&m, &t, 8, f);
                    for (int j = 0; j < 16; j++) {
                        CHECK(m.body[j] < 256 && m.white[j] < 256);
                        if (j >= 8) {
                            CHECK(m.body[j] == 0 && m.white[j] == 0);
                        }
                    }
                }
            }
        }
    }
}

static void test_slot_rebuilds_on_new_id(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 4));
    Genome *small = calloc(1, sizeof(*small));
    Genome *large = calloc(1, sizeof(*large));
    large->g[GENE_SIZE] = 1.0f;

    const SpriteMask *m = sprite_get(&c, 2, 7, small, 16, FRAME_REST0);
    SpriteMask first = *m;
    /* Same id: the old picture, whatever genome comes with it. */
    m = sprite_get(&c, 2, 7, large, 16, FRAME_REST0);
    CHECK(memcmp(&first, m, sizeof(first)) == 0);
    /* A new id in the slot: a new picture. */
    m = sprite_get(&c, 2, 8, large, 16, FRAME_REST0);
    CHECK(memcmp(&first, m, sizeof(first)) != 0);
    /* After a reset the same id is drawn again: ids restart after r. */
    sprite_cache_reset(&c);
    m = sprite_get(&c, 2, 8, small, 16, FRAME_REST0);
    CHECK(memcmp(&first, m, sizeof(first)) == 0);
    CHECK(sprite_get(&c, 4, 1, small, 16, FRAME_REST0) == NULL);
    CHECK(sprite_get(&c, -1, 1, small, 16, FRAME_REST0) == NULL);

    free(small);
    free(large);
    sprite_cache_free(&c);
    sprite_cache_free(&c);
}

/* The body drawn is still a single tile, so a huge size must only stay inside
 * it, for every frame and at both pixel sizes. */
static void test_sprite_any_size(void)
{
    BodyTraits t = traits(1000000.0f, 0.5f, 0.5f, 0.5f, 0.5f);
    for (int f = 0; f < FRAME_COUNT; f++) {
        SpriteMask large, small;
        sprite_build(&large, &t, 16, f);
        sprite_build(&small, &t, 8, f);
        int on = 0;
        for (int row = 0; row < 16; row++) {
            on += large.body[row] != 0;
        }
        CHECK(on > 0);
        /* An 8-pixel picture uses only its low 8 bits, and rows up to 8. */
        for (int row = 0; row < 16; row++) {
            CHECK((small.body[row] & 0xFF00u) == 0);
            if (row >= 8) {
                CHECK(small.body[row] == 0);
            }
        }
    }
}

/* How many bits are set in a 64-bit row. */
static int wide_bits(uint64_t row)
{
    int n = 0;
    for (; row != 0; row >>= 1) {
        n += (int)(row & 1u);
    }
    return n;
}

static int wide_mask_bits(const uint64_t *rows, int size)
{
    int n = 0;
    for (int j = 0; j < size; j++) {
        n += wide_bits(rows[j]);
    }
    return n;
}

/* The steps go up with size and never down. */
static void test_tiles_step_up_with_size(void)
{
    CHECK(sprite_tiles_for(0.5f) == 1);
    CHECK(sprite_tiles_for(1.5f) == 1);
    CHECK(sprite_tiles_for(SPRITE_TWO_TILES) == 2);
    CHECK(sprite_tiles_for(SPRITE_THREE_TILES) == 3);
    int last = 1;
    for (int k = 0; k <= 400; k++) {
        int tiles = sprite_tiles_for((float)k * 0.05f);
        CHECK(tiles >= last);
        last = tiles;
    }
    CHECK(last == SPRITE_MAX_TILES);
}

/* No size, however large or strange, asks for more than the largest mask. */
static void test_tiles_capped_for_huge(void)
{
    CHECK(sprite_tiles_for(1000000.0f) == SPRITE_MAX_TILES);
    CHECK(sprite_tiles_for(INFINITY) == SPRITE_MAX_TILES);
    CHECK(sprite_tiles_for(NAN) == 1);
    CHECK(sprite_tiles_for(-5.0f) == 1);
}

/* Every picture at every step, base size and frame keeps inside its square, and
 * its body and white never share a pixel. Parts at their largest included. */
static void test_mask_in_bounds_at_every_step(void)
{
    const int bases[2] = {16, 8};
    for (int b = 0; b < 2; b++) {
        for (int tiles = 2; tiles <= SPRITE_MAX_TILES; tiles++) {
            int size = bases[b] * tiles;
            /* Bits from size up must be clear: (1 << size) - 1 has the low
             * size bits set. 1ull: a plain 1 is an int, too narrow to shift
             * by 32 or more, which is undefined. */
            uint64_t inside = (1ull << size) - 1u;
            for (int v = 0; v <= 2; v++) {
                float x = (float)v / 2.0f;
                BodyTraits t = {x, x, x, x, x, x, x, x, x};
                for (int f = 0; f < FRAME_COUNT; f++) {
                    WideMask m;
                    sprite_build_wide(&m, &t, size, f);
                    CHECK(m.size == size);
                    CHECK(wide_mask_bits(m.body, size) > 0);
                    for (int j = 0; j < SPRITE_MAX_COLS; j++) {
                        CHECK((m.body[j] & ~inside) == 0);
                        CHECK((m.white[j] & ~inside) == 0);
                        CHECK((m.outline[j] & ~inside) == 0);
                        CHECK((m.body[j] & m.white[j]) == 0);
                        /* The outline covers the whole body. */
                        CHECK((m.body[j] & ~m.outline[j]) == 0);
                        if (j >= size) {
                            CHECK(m.body[j] == 0 && m.white[j] == 0 &&
                                  m.outline[j] == 0);
                        }
                    }
                }
            }
        }
    }
}

/* Each part gene changes the picture once it passes its step, at one tile and
 * at three; below the step the picture stays as it was. */
static void test_parts_follow_genes(void)
{
    BodyTraits plain = traits(0.6f, 0.5f, 0.3f, 0.0f, 0.0f);
    for (int part = 0; part < 4; part++) {
        BodyTraits weak = plain, strong = plain;
        float *weak_gene[4] = {&weak.armour, &weak.weapon, &weak.legs, &weak.swim};
        float *strong_gene[4] = {&strong.armour, &strong.weapon, &strong.legs,
                                 &strong.swim};
        *weak_gene[part] = 0.2f;
        *strong_gene[part] = 1.0f;
        SpriteMask a, b, c;
        sprite_build(&a, &plain, 16, FRAME_REST0);
        sprite_build(&b, &weak, 16, FRAME_REST0);
        sprite_build(&c, &strong, 16, FRAME_REST0);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0);
        CHECK(memcmp(&a, &c, sizeof(a)) != 0);
        WideMask wa, wc;
        sprite_build_wide(&wa, &plain, 48, FRAME_REST0);
        sprite_build_wide(&wc, &strong, 48, FRAME_REST0);
        CHECK(memcmp(wa.body, wc.body, sizeof(wa.body)) != 0 ||
              memcmp(wa.white, wc.white, sizeof(wa.white)) != 0);
    }
    /* Plates and tusks are white, like bone; long legs and fins are body. */
    BodyTraits armoured = plain, armed = plain;
    armoured.armour = 1.0f;
    armed.weapon = 1.0f;
    WideMask none, plates, tusks;
    sprite_build_wide(&none, &plain, 48, FRAME_REST0);
    sprite_build_wide(&plates, &armoured, 48, FRAME_REST0);
    sprite_build_wide(&tusks, &armed, 48, FRAME_REST0);
    CHECK(wide_mask_bits(plates.white, 48) > wide_mask_bits(none.white, 48));
    CHECK(wide_mask_bits(tusks.white, 48) > wide_mask_bits(none.white, 48));
}

/* More recent hits, more scar; scars only on the body; none with no hits. */
static void test_scars_follow_recent_hits(void)
{
    BodyTraits t = traits(1.0f, 0.5f, 0.3f, 0.0f, 0.0f);
    WideMask m;
    sprite_build_wide(&m, &t, 48, FRAME_REST0);
    uint64_t scars[SPRITE_MAX_COLS];
    int last = 0;
    for (int count = 0; count <= 3; count++) {
        sprite_scars(scars, m.body, 48, count);
        int n = wide_mask_bits(scars, 48);
        CHECK(count == 0 ? n == 0 : n > last);
        last = n;
        for (int j = 0; j < SPRITE_MAX_COLS; j++) {
            CHECK((scars[j] & ~m.body[j]) == 0);
        }
    }
    /* More than three is drawn as three. */
    uint64_t many[SPRITE_MAX_COLS];
    sprite_scars(many, m.body, 48, 100);
    CHECK(memcmp(many, scars, sizeof(many)) == 0);
    /* An empty body gets nothing, and the 16-pixel body gets some. */
    uint64_t empty[SPRITE_MAX_COLS] = {0};
    sprite_scars(scars, empty, 48, 3);
    CHECK(wide_mask_bits(scars, 48) == 0);
    SpriteMask small;
    sprite_build(&small, &t, 16, FRAME_REST0);
    uint64_t rows[SPRITE_MAX_COLS] = {0};
    for (int j = 0; j < 16; j++) {
        rows[j] = small.body[j];
    }
    sprite_scars(scars, rows, 16, 2);
    CHECK(wide_mask_bits(scars, 16) > 0);
}

/* The large bodies' table: the same entry for the same body, a new picture for
 * a new size, nothing for sizes it does not draw, and many more bodies than
 * entries only cost redrawing. */
static void test_wide_cache(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 4));
    Genome *g = calloc(1, sizeof(*g));
    g->g[GENE_ARMOUR] = 1.0f;
    const WideMask *two = sprite_get_wide(&c, 9, g, 2, 16, FRAME_REST0);
    CHECK(two != NULL && two->size == 32);
    CHECK(sprite_get_wide(&c, 9, g, 2, 16, FRAME_REST0) == two);
    const WideMask *three = sprite_get_wide(&c, 9, g, 3, 8, FRAME_WALK1);
    CHECK(three != NULL && three->size == 24);
    CHECK(sprite_get_wide(&c, 9, g, 1, 16, FRAME_REST0) == NULL);
    CHECK(sprite_get_wide(&c, 9, g, 4, 16, FRAME_REST0) == NULL);
    CHECK(sprite_get_wide(&c, 9, g, 2, 12, FRAME_REST0) == NULL);
    CHECK(sprite_get_wide(&c, 9, g, 2, 16, FRAME_COUNT) == NULL);
    WideMask first = *sprite_get_wide(&c, 9, g, 2, 16, FRAME_REST0);
    for (uint32_t id = 100; id < 100 + 3 * SPRITE_BIG_ENTRIES; id++) {
        CHECK(sprite_get_wide(&c, id, g, 2, 16, FRAME_REST0) != NULL);
    }
    /* Pushed out long ago, and drawn again the same. */
    const WideMask *again = sprite_get_wide(&c, 9, g, 2, 16, FRAME_REST0);
    CHECK(memcmp(&first, again, sizeof(first)) == 0);
    sprite_cache_reset(&c);
    CHECK(sprite_get_wide(&c, 9, g, 2, 16, FRAME_REST0) != NULL);
    free(g);
    sprite_cache_free(&c);
    sprite_cache_free(&c);
}

int main(void)
{
    test_same_traits_same_pixels();
    test_bigger_size_more_body_pixels();
    test_speed_sets_leg_count();
    test_meat_gives_teeth_plant_gives_spots();
    test_body_and_white_never_overlap();
    test_sight_changes_eye();
    test_small_mutation_same_picture();
    test_walk_frames_differ();
    test_every_frame_stays_in_tile();
    test_slot_rebuilds_on_new_id();
    test_sprite_any_size();
    test_tiles_step_up_with_size();
    test_tiles_capped_for_huge();
    test_mask_in_bounds_at_every_step();
    test_parts_follow_genes();
    test_scars_follow_recent_hits();
    test_wide_cache();
    TEST_SUMMARY();
}
