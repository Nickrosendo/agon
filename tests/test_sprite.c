#include "test.h"

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
    BodyTraits t = {size, speed, sight, plant, meat};
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
    BodyTraits t = {1000000.0f, 0.5f, 0.5f, 0.5f, 0.5f};
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
    TEST_SUMMARY();
}
