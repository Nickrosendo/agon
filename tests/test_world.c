#include "test.h"

#include <math.h>   /* fabsf, fmaxf */
#include <string.h> /* memcmp */

#include "rng.h"
#include "settings.h"
#include "terrain.h"
#include "world.h"

static void test_index_wraps(void)
{
    World w;
    CHECK(world_init(&w, 8, 4));
    CHECK(world_idx(&w, 0, 0) == 0);
    CHECK(world_idx(&w, -1, 0) == 7);
    CHECK(world_idx(&w, 8, 4) == 0);
    CHECK(world_idx(&w, 3, -1) == 27);
    world_free(&w);
}

static void test_bad_sizes_fail(void)
{
    World w;
    CHECK(!world_init(&w, 0, 4));
    CHECK(!world_init(&w, 8, 0));
    CHECK(!world_init(&w, -3, 4));
    CHECK(!world_init(&w, 8, -1));
    CHECK(!world_init(&w, 100000, 100000)); /* 10^10 cells: too many */
    /* "Nothing to free": every pointer is NULL. The memory checker would
     * also report a leak at exit if something had been allocated. */
    CHECK(w.terrain == NULL && w.occupant == NULL && w.carrion == NULL);
}

static void test_layers_start_empty(void)
{
    World w;
    CHECK(world_init(&w, 16, 8));
    int ok = 1;
    for (int i = 0; i < w.w * w.h; i++) {
        if (w.occupant[i] != -1 || w.terrain[i] != 0 ||
            w.fertility[i] != 0.0f || w.veg[i] != 0 || w.veg_next[i] != 0 ||
            w.veg_age[i] != 0 || w.veg_wither[i] != 0 ||
            w.plant[i].energy != 0.0f || w.carrion[i] != 0.0f ||
            w.carrion_level[i] != 0.0f) {
            ok = 0;
        }
    }
    CHECK(ok);
    world_free(&w);
    CHECK(w.terrain == NULL);
    world_free(&w); /* a second free must be harmless */
}

/* Builds a 64 by 32 world from a seed with the default settings. */
static void generate(World *w, uint64_t seed)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, seed);
    CHECK(world_init(w, 64, 32));
    terrain_generate(w, &r, &s);
}

static void test_same_seed_same_terrain(void)
{
    World a, b, c;
    generate(&a, 42);
    generate(&b, 42);
    generate(&c, 43);
    size_t n = (size_t)a.w * (size_t)a.h;
    /* memcmp compares raw bytes and returns 0 when they are identical. */
    CHECK(memcmp(a.terrain, b.terrain, n) == 0);
    CHECK(memcmp(a.fertility, b.fertility, n * sizeof(float)) == 0);
    CHECK(memcmp(a.terrain, c.terrain, n) != 0);
    world_free(&a);
    world_free(&b);
    world_free(&c);
}

static void test_soil_share(void)
{
    World w;
    generate(&w, 42);
    int soil = 0;
    int fertility_ok = 1;
    for (int i = 0; i < w.w * w.h; i++) {
        if (w.terrain[i] == TERRAIN_SOIL) {
            soil++;
        } else if (w.fertility[i] != 0.0f) {
            fertility_ok = 0; /* only soil may be fertile */
        }
        if (w.fertility[i] < 0.0f || w.fertility[i] > 1.0f) {
            fertility_ok = 0;
        }
    }
    double share = (double)soil / (w.w * w.h);
    CHECK(share >= 0.30 && share <= 0.80);
    CHECK(fertility_ok);
    world_free(&w);
}

/* The world wraps, so the noise must too: the step from the last column
 * to the first must be no bigger than the biggest step between two
 * neighbouring columns inside the world. Same for rows. */
static void test_noise_tiles(void)
{
    const int w = 96, h = 48;
    float biggest_inside = 0.0f, biggest_seam = 0.0f;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float here = terrain_noise(7, x, y, w, h, 16.0f, 4);
            float right = terrain_noise(7, (x + 1) % w, y, w, h, 16.0f, 4);
            float below = terrain_noise(7, x, (y + 1) % h, w, h, 16.0f, 4);
            float step_right = fabsf(right - here);
            float step_down = fabsf(below - here);
            if (x == w - 1) {
                biggest_seam = fmaxf(biggest_seam, step_right);
            } else {
                biggest_inside = fmaxf(biggest_inside, step_right);
            }
            if (y == h - 1) {
                biggest_seam = fmaxf(biggest_seam, step_down);
            } else {
                biggest_inside = fmaxf(biggest_inside, step_down);
            }
        }
    }
    CHECK(biggest_seam <= biggest_inside);
    CHECK(biggest_inside > 0.0f); /* the noise is not flat */
}

int main(void)
{
    test_index_wraps();
    test_bad_sizes_fail();
    test_layers_start_empty();
    test_same_seed_same_terrain();
    test_soil_share();
    test_noise_tiles();
    TEST_SUMMARY();
}
