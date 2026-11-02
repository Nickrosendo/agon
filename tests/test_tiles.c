#include "test.h"

#include <string.h> /* memcmp */

#include "tiles.h"
#include "world.h"  /* VEG_MAX */

static int mask_bits(const uint16_t rows[16])
{
    int n = 0;
    for (int j = 0; j < 16; j++) {
        for (unsigned r = rows[j]; r != 0; r >>= 1) {
            n += (int)(r & 1u);
        }
    }
    return n;
}

static void test_same_inputs_same_mask(void)
{
    for (int kind = 0; kind < TILE_KINDS; kind++) {
        uint16_t a[16], b[16];
        tiles_build(a, kind, 2, 1, 16, 1);
        tiles_build(b, kind, 2, 1, 16, 1);
        CHECK(memcmp(a, b, sizeof(a)) == 0); /* real arrays: sizeof is 32 */
        CHECK(mask_bits(a) > 0);
    }
}

static void test_variant_is_stable_and_spread(void)
{
    int seen[TILE_VARIANTS] = {0};
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int v = tiles_variant(x, y);
            CHECK(v >= 0 && v < TILE_VARIANTS);
            CHECK(v == tiles_variant(x, y));
            if (v >= 0 && v < TILE_VARIANTS) {
                seen[v]++;
            }
        }
    }
    /* Each of the four about a quarter of the time, not merely once. */
    for (int v = 0; v < TILE_VARIANTS; v++) {
        CHECK(seen[v] > 64 * 64 / 8);
    }
}

static void test_plant_grows_with_density(void)
{
    for (int size = 8; size <= 16; size += 8) {
        for (int v = 0; v < TILE_VARIANTS; v++) {
            int before = -1;
            for (int d = 0; d <= VEG_MAX; d++) {
                uint16_t m[16];
                tiles_build(m, TILE_PLANT, v, d, size, 0);
                int n = mask_bits(m);
                CHECK(n > before);
                if (d == 0) {
                    CHECK(n == 0);
                }
                before = n;
            }
        }
    }
}

static void test_fertile_soil_more_dots(void)
{
    for (int v = 0; v < TILE_VARIANTS; v++) {
        int before = 0;
        for (int d = 0; d < TILE_SOIL_DETAILS; d++) {
            uint16_t m[16];
            tiles_build(m, TILE_SOIL, v, d, 16, 0);
            CHECK(mask_bits(m) == d + 1);
            CHECK(mask_bits(m) > before);
            before = mask_bits(m);
        }
    }
}

static void test_water_frames_differ(void)
{
    uint16_t a[16], b[16];
    tiles_build(a, TILE_WATER, 0, 0, 16, 0);
    tiles_build(b, TILE_WATER, 0, 0, 16, 1);
    CHECK(memcmp(a, b, sizeof(a)) != 0);
}

static void test_marks_change_over_their_frames(void)
{
    const int kinds[2] = {TILE_SLASH, TILE_CRUMBS};
    for (int k = 0; k < 2; k++) {
        for (int size = 8; size <= 16; size += 8) {
            for (int f = 0; f < TILE_MARK_FRAMES; f++) {
                uint16_t a[16], b[16];
                tiles_build(a, kinds[k], 0, 0, size, f);
                tiles_build(b, kinds[k], 0, 0, size,
                            (f + 1) % TILE_MARK_FRAMES);
                CHECK(mask_bits(a) > 0);
                CHECK(memcmp(a, b, sizeof(a)) != 0);
            }
        }
    }
}

static void test_size_8_stays_in_tile(void)
{
    for (int kind = 0; kind < TILE_KINDS; kind++) {
        for (int v = 0; v < TILE_VARIANTS; v++) {
            for (int d = 0; d <= VEG_MAX; d++) {
                for (int f = 0; f < TILE_WATER_FRAMES; f++) {
                    uint16_t m[16];
                    tiles_build(m, kind, v, d, 8, f);
                    for (int j = 0; j < 16; j++) {
                        CHECK(m[j] < 256);
                        if (j >= 8) {
                            CHECK(m[j] == 0);
                        }
                    }
                }
            }
        }
    }
}

int main(void)
{
    test_same_inputs_same_mask();
    test_variant_is_stable_and_spread();
    test_plant_grows_with_density();
    test_fertile_soil_more_dots();
    test_water_frames_differ();
    test_marks_change_over_their_frames();
    test_size_8_stays_in_tile();
    TEST_SUMMARY();
}
