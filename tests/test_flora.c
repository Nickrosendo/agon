#include "test.h"

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

int main(void)
{
    test_line_of_three();
    test_lone_cell_grows_to_max();
    test_crowded_cell_shrinks();
    test_water_and_rock_stay_bare();
    test_barren_world_stays_empty();
    TEST_SUMMARY();
}
