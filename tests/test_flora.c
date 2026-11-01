#include "test.h"

#include <string.h> /* memcpy */

#include "flora.h"
#include "rng.h"
#include "settings.h"
#include "world.h"

/* A world of all soil with fertility 1, and no vegetation. */
static void make_world(World *w, int width, int height)
{
    CHECK(world_init(w, width, height));
    for (int i = 0; i < width * height; i++) {
        w->terrain[i] = TERRAIN_SOIL;
        w->fertility[i] = 1.0f;
    }
}

/* Default settings with the rule's numbers replaced, and no seeding from
 * nothing, so every change in these tests comes from the rule itself. */
static Settings rule(int birth_lo, int birth_hi, int crowd_max)
{
    Settings s = settings_default();
    s.veg_birth_lo = birth_lo;
    s.veg_birth_hi = birth_hi;
    s.veg_crowd_max = crowd_max;
    s.veg_seed_chance = 0.0f;
    /* Out of reach: these tests are about the neighbour rule alone. */
    s.veg_capacity = 1000.0f;
    return s;
}

static uint8_t veg_at(const World *w, int x, int y)
{
    return w->veg[world_idx(w, x, y)];
}

static void test_line_of_three(void)
{
    World w;
    make_world(&w, 9, 9);
    Settings s = rule(3, 3, 3);
    Rng r;
    rng_seed(&r, 1);
    w.veg[world_idx(&w, 4, 3)] = 1;
    w.veg[world_idx(&w, 4, 4)] = 1;
    w.veg[world_idx(&w, 4, 5)] = 1;

    flora_step(&w, &r, &s);

    /* The middle's two side cells have 3 planted neighbours: they sprout
     * (fertility 1 makes it certain). */
    CHECK(veg_at(&w, 3, 4) == 1);
    CHECK(veg_at(&w, 5, 4) == 1);
    /* The original three have 1 or 2 neighbours, within the crowd limit:
     * they grow. */
    CHECK(veg_at(&w, 4, 3) == 2);
    CHECK(veg_at(&w, 4, 4) == 2);
    CHECK(veg_at(&w, 4, 5) == 2);
    /* Beside the ends there are only 2 neighbours: nothing sprouts. */
    CHECK(veg_at(&w, 3, 3) == 0);
    CHECK(veg_at(&w, 4, 2) == 0);
    world_free(&w);
}

static void test_lone_cell_grows_to_max(void)
{
    World w;
    make_world(&w, 7, 7);
    Settings s = rule(3, 3, 3);
    Rng r;
    rng_seed(&r, 2);
    w.veg[world_idx(&w, 3, 3)] = 1;
    for (int i = 0; i < 3; i++) {
        flora_step(&w, &r, &s);
    }
    CHECK(veg_at(&w, 3, 3) == VEG_MAX);
    for (int i = 0; i < 5; i++) {
        flora_step(&w, &r, &s);
    }
    CHECK(veg_at(&w, 3, 3) == VEG_MAX);
    world_free(&w);
}

static void test_crowded_cell_shrinks(void)
{
    World w;
    make_world(&w, 9, 9);
    /* Birth range 9..9 can never happen, so nothing sprouts around. */
    Settings s = rule(9, 9, 3);
    Rng r;
    rng_seed(&r, 3);
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            w.veg[world_idx(&w, 4 + dx, 4 + dy)] = VEG_MAX;
        }
    }
    /* The centre has 8 planted neighbours. The ring cells themselves
     * have 3 or 5 and change too, but stay planted for these steps, so
     * the centre keeps seeing 8. */
    for (int expected = VEG_MAX - 1; expected >= VEG_MAX - 3; expected--) {
        flora_step(&w, &r, &s);
        CHECK(veg_at(&w, 4, 4) == expected);
    }
    world_free(&w);
}

static void test_water_and_rock_stay_bare(void)
{
    World w;
    make_world(&w, 9, 9);
    Settings s = rule(1, 8, 8);
    s.veg_seed_chance = 1.0f; /* even forced sprouting must not reach them */
    Rng r;
    rng_seed(&r, 4);
    int water = world_idx(&w, 2, 2);
    int rock = world_idx(&w, 6, 6);
    w.terrain[water] = TERRAIN_WATER;
    w.terrain[rock] = TERRAIN_ROCK;
    for (int i = 0; i < w.w * w.h; i++) {
        if (w.terrain[i] == TERRAIN_SOIL) {
            w.veg[i] = 2;
        }
    }
    for (int i = 0; i < 10; i++) {
        flora_step(&w, &r, &s);
        CHECK(w.veg[water] == 0 && w.veg[rock] == 0);
    }
    world_free(&w);
}

static void test_barren_world_stays_empty(void)
{
    World w;
    make_world(&w, 16, 16);
    Settings s = rule(3, 3, 3);
    Rng r;
    rng_seed(&r, 5);
    for (int i = 0; i < w.w * w.h; i++) {
        w.fertility[i] = 0.0f;
    }
    int empty = 1;
    for (int step = 0; step < 100; step++) {
        flora_step(&w, &r, &s);
    }
    for (int i = 0; i < w.w * w.h; i++) {
        if (w.veg[i] != 0) {
            empty = 0;
        }
    }
    CHECK(empty);
    world_free(&w);
}

/* Fills every soil cell around (cx, cy) within the reach, except the
 * centre, with the given density. */
static void ring(World *w, int cx, int cy, int reach, uint8_t density)
{
    for (int dy = -reach; dy <= reach; dy++) {
        for (int dx = -reach; dx <= reach; dx++) {
            if (dx != 0 || dy != 0) {
                w->veg[world_idx(w, cx + dx, cy + dy)] = density;
            }
        }
    }
}

static void test_capacity_blocks_sprouting(void)
{
    /* Reach 2: 24 cells around the centre. All at density 1 gives
     * m = 24. The centre has 8 planted touching neighbours, so the birth
     * range 1..8 would let it sprout, but capacity 20 * fertility 1 is
     * below 24. */
    World w;
    make_world(&w, 11, 11);
    Settings s = rule(1, 8, 8);
    s.veg_reach = 2;
    s.veg_capacity = 20.0f;
    Rng r;
    rng_seed(&r, 6);
    ring(&w, 5, 5, 2, 1);
    flora_step(&w, &r, &s);
    CHECK(veg_at(&w, 5, 5) == 0);

    /* Same neighbourhood with room to spare: it sprouts. */
    world_free(&w);
    make_world(&w, 11, 11);
    s.veg_capacity = 30.0f;
    ring(&w, 5, 5, 2, 1);
    flora_step(&w, &r, &s);
    CHECK(veg_at(&w, 5, 5) == 1);
    world_free(&w);
}

static void test_capacity_scales_with_fertility(void)
{
    /* m = 24 again, capacity 40: room at fertility 1 (limit 40), none at
     * fertility 0.5 (limit 20). The neighbourhood fertility does not
     * matter, only the cell's own. */
    World w;
    make_world(&w, 11, 11);
    Settings s = rule(1, 8, 8);
    s.veg_reach = 2;
    s.veg_capacity = 40.0f;
    Rng r;
    rng_seed(&r, 7);
    ring(&w, 5, 5, 2, 1);
    w.fertility[world_idx(&w, 5, 5)] = 0.5f;
    /* Fertility 0.5 also halves the chance to sprout, so try several
     * steps on a fresh copy each time: it must never sprout. */
    int ever_sprouted = 0;
    for (int i = 0; i < 20; i++) {
        uint8_t saved[121];
        memcpy(saved, w.veg, sizeof(saved));
        flora_step(&w, &r, &s);
        if (veg_at(&w, 5, 5) != 0) {
            ever_sprouted = 1;
        }
        memcpy(w.veg, saved, sizeof(saved));
    }
    CHECK(!ever_sprouted);
    world_free(&w);
}

static void test_over_capacity_shrinks(void)
{
    /* A planted centre with room among its touching neighbours (crowd
     * limit 8) but m = 24 * 2 = 48 above capacity 30: it thins by one per
     * step instead of growing. */
    World w;
    make_world(&w, 11, 11);
    Settings s = rule(9, 9, 8);
    s.veg_reach = 2;
    s.veg_capacity = 30.0f;
    Rng r;
    rng_seed(&r, 8);
    ring(&w, 5, 5, 2, 2);
    w.veg[world_idx(&w, 5, 5)] = 3;
    flora_step(&w, &r, &s);
    CHECK(veg_at(&w, 5, 5) == 2);
    world_free(&w);
}

static void test_tiny_world_is_safe(void)
{
    /* A 2 by 2 world with a reach of 3, larger than the world: without
     * the clamp in flora_step this reads outside the layer, and the
     * memory checker stops the test with a heap-buffer-overflow report. */
    World w;
    make_world(&w, 2, 2);
    Settings s = rule(1, 8, 8);
    s.veg_reach = 3;
    Rng r;
    rng_seed(&r, 9);
    w.veg[0] = 1;
    for (int i = 0; i < 10; i++) {
        flora_step(&w, &r, &s);
    }
    CHECK(w.veg[0] <= VEG_MAX);
    world_free(&w);
}

int main(void)
{
    test_line_of_three();
    test_lone_cell_grows_to_max();
    test_crowded_cell_shrinks();
    test_water_and_rock_stay_bare();
    test_barren_world_stays_empty();
    test_capacity_blocks_sprouting();
    test_capacity_scales_with_fertility();
    test_over_capacity_shrinks();
    test_tiny_world_is_safe();
    TEST_SUMMARY();
}
