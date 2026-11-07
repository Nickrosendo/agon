#include "test.h"

#include <math.h>   /* fabsf */
#include <string.h> /* memcpy */

#include "flora.h"
#include "rng.h"
#include "settings.h"
#include "world.h"
#include "years.h" /* SEASON_SUMMER: "as today" in the design */

/* A world of all soil with fertility 1, full water, a middling temperature, and
 * no vegetation. */
static void make_world(World *w, int width, int height)
{
    CHECK(world_init(w, width, height));
    for (int i = 0; i < width * height; i++) {
        w->terrain[i] = TERRAIN_SOIL;
        w->fertility[i] = 1.0f;
        w->water[i] = 1.0f;
        w->temperature[i] = 0.5f;
    }
}

/* A plant that suits make_world's soil perfectly, with plenty stored. */
static Plant healthy_plant(void)
{
    return (Plant){.energy = 10.0f, .water_need = 0.5f, .temp_pref = 0.5f};
}

/* Default settings with the plants' life taken out of the way: no mutation, no
 * cost for anything, and sprouting and growth certain whenever they are allowed,
 * so every change in these tests comes from the one rule each is about. */
static Settings rules(void)
{
    Settings s = settings_default();
    s.plant_mutation = 0.0f;
    s.plant_upkeep = 0.0f;
    s.plant_seed_cost = 0.0f;
    s.plant_grow_cost = 0.0f;
    s.plant_fruit_rate = 0.0f;
    s.plant_fruit_near = 1.0f;
    /* Out of reach: these tests are about one rule alone. */
    s.veg_capacity = 1000.0f;
    s.veg_wither_chance = 0.0f; /* plants never wither */
    s.veg_sprout_rate = 1.0f;
    s.veg_grow_chance = 1.0f;
    return s;
}

/* Every cell holds a healthy plant's numbers, ready for veg to switch it on. */
static void plants_everywhere(World *w)
{
    for (int i = 0; i < w->w * w->h; i++) {
        w->plant[i] = healthy_plant();
    }
}

static uint8_t veg_at(const World *w, int x, int y)
{
    return w->veg[world_idx(w, x, y)];
}

static void test_lone_cell_grows_to_max(void)
{
    World w;
    make_world(&w, 7, 7);
    Settings s = rules();
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 2);
    w.veg[world_idx(&w, 3, 3)] = 1;
    for (int i = 0; i < 3; i++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    CHECK(veg_at(&w, 3, 3) == VEG_MAX);
    for (int i = 0; i < 5; i++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    CHECK(veg_at(&w, 3, 3) == VEG_MAX);
    world_free(&w);
}

static void test_water_and_rock_stay_bare(void)
{
    World w;
    make_world(&w, 9, 9);
    Settings s = rules();
    plants_everywhere(&w);
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
        flora_step(&w, &r, &s, SEASON_SUMMER);
        CHECK(w.veg[water] == 0 && w.veg[rock] == 0);
    }
    world_free(&w);
}

static void test_barren_world_stays_empty(void)
{
    World w;
    make_world(&w, 16, 16);
    Settings s = rules();
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 5);
    for (int i = 0; i < w.w * w.h; i++) {
        w.fertility[i] = 0.0f;
    }
    int empty = 1;
    for (int step = 0; step < 100; step++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
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

/* Empties the four cells that touch (cx, cy) diagonally. */
static void clear_corners(World *w, int cx, int cy)
{
    w->veg[world_idx(w, cx - 1, cy - 1)] = 0;
    w->veg[world_idx(w, cx + 1, cy - 1)] = 0;
    w->veg[world_idx(w, cx - 1, cy + 1)] = 0;
    w->veg[world_idx(w, cx + 1, cy + 1)] = 0;
}

/* How many cells of the world carry any vegetation. */
static int planted_cells(const World *w)
{
    int count = 0;
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->veg[i] > 0) {
            count++;
        }
    }
    return count;
}

static void test_capacity_blocks_sprouting(void)
{
    /* Reach 2: 24 cells around the centre; without the four corners
     * that touch it, 20 at density 1 give m = 20. The centre then has 4
     * planted touching neighbours, inside the birth range 1..4, but
     * capacity 20 * fertility 1 leaves no room (m must stay below it). */
    World w;
    make_world(&w, 11, 11);
    Settings s = rules();
    s.veg_reach = 2;
    s.veg_capacity = 20.0f;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 6);
    ring(&w, 5, 5, 2, 1);
    clear_corners(&w, 5, 5);
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(veg_at(&w, 5, 5) == 0);

    /* Same neighbourhood with room to spare: it sprouts. */
    world_free(&w);
    make_world(&w, 11, 11);
    plants_everywhere(&w);
    s.veg_capacity = 30.0f;
    ring(&w, 5, 5, 2, 1);
    clear_corners(&w, 5, 5);
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(veg_at(&w, 5, 5) == 1);
    world_free(&w);
}

static void test_planted_cells_cap(void)
{
    /* Eight planted cells around (5, 5). With birth range 1..4 the cells
     * around the ring (1 to 3 planted neighbours each) would sprout. */
    World w;
    Settings s = rules();
    Rng r;
    rng_seed(&r, 6);

    /* A cap of 8 is already reached: nothing sprouts, and the eight
     * plants stay. */
    make_world(&w, 11, 11);
    plants_everywhere(&w);
    s.veg_max_cells = 8;
    ring(&w, 5, 5, 1, 1);
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(planted_cells(&w) == 8);
    world_free(&w);

    /* A cap of 9 is not reached at the start of the tick, so sprouting
     * goes ahead; the count is taken once per tick, so it can overshoot. */
    make_world(&w, 11, 11);
    plants_everywhere(&w);
    s.veg_max_cells = 9;
    ring(&w, 5, 5, 1, 1);
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(planted_cells(&w) > 9);
    world_free(&w);

    /* 0 means no cap. */
    make_world(&w, 11, 11);
    plants_everywhere(&w);
    s.veg_max_cells = 0;
    ring(&w, 5, 5, 1, 1);
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(planted_cells(&w) > 8);
    world_free(&w);
}

static void test_capacity_scales_with_fertility(void)
{
    /* m = 20 again (the ring without its four corners), capacity 40:
     * room at fertility 1 (limit 40), none at fertility 0.5 (limit 20).
     * The neighbourhood fertility does not matter, only the cell's own. */
    World w;
    make_world(&w, 11, 11);
    Settings s = rules();
    s.veg_reach = 2;
    s.veg_capacity = 40.0f;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 7);
    ring(&w, 5, 5, 2, 1);
    clear_corners(&w, 5, 5);
    w.fertility[world_idx(&w, 5, 5)] = 0.5f;
    /* Fertility 0.5 also halves the chance to sprout, so try several
     * steps on a fresh copy each time: it must never sprout. */
    int ever_sprouted = 0;
    for (int i = 0; i < 20; i++) {
        uint8_t saved[121];
        memcpy(saved, w.veg, sizeof(saved));
        flora_step(&w, &r, &s, SEASON_SUMMER);
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
    /* A planted centre with room among its touching neighbours (7 of
     * them planted, crowd limit 7) but m = 23 * 2 = 46 above capacity 30:
     * it thins by one per step instead of growing. */
    World w;
    make_world(&w, 11, 11);
    Settings s = rules();
    s.veg_reach = 2;
    s.veg_capacity = 30.0f;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 8);
    ring(&w, 5, 5, 2, 2);
    w.veg[world_idx(&w, 4, 4)] = 0;
    w.veg[world_idx(&w, 5, 5)] = 3;
    flora_step(&w, &r, &s, SEASON_SUMMER);
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
    Settings s = rules();
    s.veg_reach = 3;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 9);
    w.veg[0] = 1;
    for (int i = 0; i < 10; i++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    CHECK(w.veg[0] <= VEG_MAX);
    world_free(&w);
}

static void test_plant_withers_then_disappears(void)
{
    /* Mature at age 2, certain to start withering once mature, withering
     * lasts 3 ticks. Nothing sprouts around it: its neighbours have one
     * planted neighbour each, outside the birth range 4..4. */
    World w;
    make_world(&w, 7, 7);
    Settings s = rules();
    s.veg_mature_age = 2;
    s.veg_wither_chance = 1.0f;
    s.veg_wither_ticks = 3;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 10);
    int i = world_idx(&w, 3, 3);
    w.veg[i] = 1;

    flora_step(&w, &r, &s, SEASON_SUMMER); /* age 1: not mature yet, grows */
    CHECK(w.veg[i] == 2 && w.veg_age[i] == 1 && w.veg_wither[i] == 0);
    flora_step(&w, &r, &s, SEASON_SUMMER); /* age 2: mature, starts withering */
    CHECK(w.veg[i] == 2 && w.veg_wither[i] == 1);
    flora_step(&w, &r, &s, SEASON_SUMMER); /* withering: no growth */
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(w.veg[i] == 2 && w.veg_wither[i] == 3);
    flora_step(&w, &r, &s, SEASON_SUMMER); /* withering is over: gone */
    CHECK(w.veg[i] == 0 && w.veg_age[i] == 0 && w.veg_wither[i] == 0);
    world_free(&w);
}

static void test_no_wither_chance_lives_on(void)
{
    World w;
    make_world(&w, 7, 7);
    Settings s = rules();
    s.veg_mature_age = 1;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 11);
    int i = world_idx(&w, 3, 3);
    w.veg[i] = 1;
    for (int step = 0; step < 300; step++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    CHECK(w.veg[i] == VEG_MAX && w.veg_wither[i] == 0);
    CHECK(w.veg_age[i] == 300);
    world_free(&w);
}

static void test_sprout_starts_young(void)
{
    /* A cell that sprouts gets age 0 and is healthy, even if the plant
     * that grew there before left its numbers behind. */
    World w;
    make_world(&w, 9, 9);
    Settings s = rules();
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 12);
    w.veg[world_idx(&w, 4, 3)] = 1;
    w.veg[world_idx(&w, 4, 4)] = 1;
    w.veg[world_idx(&w, 4, 5)] = 1;
    int side = world_idx(&w, 3, 4);
    w.veg_age[side] = 500;
    w.veg_wither[side] = 7;
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(w.veg[side] == 1 && w.veg_age[side] == 0 && w.veg_wither[side] == 0);
    world_free(&w);
}

static void test_growth_follows_its_chance(void)
{
    /* Every other column planted at density 1: 2,048 plants, each with
     * 2 planted neighbours (above and below), well inside the crowd
     * limit 7, and capacity out of reach. The empty cells have 6 planted
     * neighbours, outside the birth range 4..4, so they stay empty.
     * After one step the share of plants that grew should be close to
     * veg_grow_chance. */
    World w;
    make_world(&w, 64, 64);
    Settings s = rules();
    s.veg_grow_chance = 0.25f;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 13);
    for (int y = 0; y < w.h; y++) {
        for (int x = 0; x < w.w; x += 2) {
            w.veg[world_idx(&w, x, y)] = 1;
        }
    }
    flora_step(&w, &r, &s, SEASON_SUMMER);
    int grown = 0;
    for (int i = 0; i < w.w * w.h; i++) {
        grown += w.veg[i] == 2;
    }
    double share = (double)grown / (w.w * w.h / 2);
    CHECK(share > 0.22 && share < 0.28);

    /* A chance of 0: nothing ever thickens. */
    s.veg_grow_chance = 0.0f;
    for (int step = 0; step < 50; step++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    int above_two = 0;
    for (int i = 0; i < w.w * w.h; i++) {
        above_two += w.veg[i] > 2;
    }
    CHECK(above_two == 0);
    world_free(&w);
}

static void test_sprouting_follows_its_rate(void)
{
    /* A checkerboard: every empty cell has exactly 4 planted neighbours
     * (the four that touch its sides), inside the birth range 4..4.
     * Fertility 1 and rate 0.25: about a quarter of the 2,048 empty cells
     * sprout in one step. */
    World w;
    make_world(&w, 64, 64);
    Settings s = rules();
    s.veg_sprout_rate = 0.25f;
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 14);
    for (int y = 0; y < w.h; y++) {
        for (int x = 0; x < w.w; x++) {
            w.veg[world_idx(&w, x, y)] = (x + y) % 2 == 0 ? 1 : 0;
        }
    }
    flora_step(&w, &r, &s, SEASON_SUMMER);
    int sprouted = 0;
    for (int y = 0; y < w.h; y++) {
        for (int x = 0; x < w.w; x++) {
            if ((x + y) % 2 == 1) {
                sprouted += veg_at(&w, x, y) > 0;
            }
        }
    }
    double share = (double)sprouted / (w.w * w.h / 2);
    CHECK(share > 0.22 && share < 0.28);
    world_free(&w);
}

/* Three plants in a column at (4, 3), (4, 4), (4, 5), told apart by their
 * fruit_share; the cells beside the middle one, (3, 4) and (5, 4), have three
 * planted neighbours. */
static void column_of_three(World *w, float top, float middle, float bottom)
{
    int a = world_idx(w, 4, 3), b = world_idx(w, 4, 4), c = world_idx(w, 4, 5);
    w->veg[a] = w->veg[b] = w->veg[c] = 1;
    w->plant[a].fruit_share = top;
    w->plant[b].fruit_share = middle;
    w->plant[c].fruit_share = bottom;
}

static void test_sprout_copies_its_parent(void)
{
    World w;
    make_world(&w, 9, 9);
    Settings s = rules();
    plants_everywhere(&w);
    Rng r;
    rng_seed(&r, 15);
    column_of_three(&w, 0.25f, 0.25f, 0.25f);
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(veg_at(&w, 3, 4) == 1 && veg_at(&w, 5, 4) == 1);
    CHECK(w.plant[world_idx(&w, 3, 4)].fruit_share == 0.25f);
    CHECK(w.plant[world_idx(&w, 5, 4)].fruit_share == 0.25f);
    /* It starts with the seed's energy, not its parent's. */
    CHECK(w.plant[world_idx(&w, 3, 4)].energy == s.plant_seed_start);
    world_free(&w);
}

static void test_sprout_picks_one_neighbour(void)
{
    /* Three different plants: over many tries, each sprout copies one of the
     * three, and each of the three is picked sometimes. */
    float shares[3] = {0.1f, 0.5f, 0.9f};
    int picked[3] = {0, 0, 0};
    int foreign = 0;
    Settings s = rules();
    Rng r;
    rng_seed(&r, 16);
    for (int attempt = 0; attempt < 60; attempt++) {
        World w;
        make_world(&w, 9, 9);
        plants_everywhere(&w);
        column_of_three(&w, shares[0], shares[1], shares[2]);
        flora_step(&w, &r, &s, SEASON_SUMMER);
        float got = w.plant[world_idx(&w, 3, 4)].fruit_share;
        int known = 0;
        for (int k = 0; k < 3; k++) {
            if (got == shares[k]) {
                picked[k]++;
                known = 1;
            }
        }
        foreign += !known;
        world_free(&w);
    }
    CHECK(foreign == 0);
    CHECK(picked[0] > 0 && picked[1] > 0 && picked[2] > 0);
}

/* A seed takes its parent's genes, each moved a little, and stays within 0..1
 * however often it mutates. Energy and fruit are not inherited. */
static void test_seed_inherits_and_mutates(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 17);
    Plant parent = {.energy = 9.0f, .fruit = 2.0f, .fruit_share = 0.5f,
                    .water_need = 0.25f, .temp_pref = 0.75f, .gut_time = 0.5f};
    Plant seed;
    s.plant_mutation = 0.0f;
    plant_make_seed(&seed, &parent, &r, &s);
    CHECK(seed.fruit_share == 0.5f && seed.water_need == 0.25f);
    CHECK(seed.temp_pref == 0.75f && seed.gut_time == 0.5f);
    CHECK(seed.energy == 0.0f && seed.fruit == 0.0f);

    s.plant_mutation = 1.0f;
    s.plant_mutation_spread = 0.5f;
    int outside = 0;
    int moved = 0;
    for (int i = 0; i < 1000; i++) {
        plant_make_seed(&seed, &parent, &r, &s);
        outside += seed.fruit_share < 0.0f || seed.fruit_share > 1.0f;
        outside += seed.water_need < 0.0f || seed.water_need > 1.0f;
        outside += seed.temp_pref < 0.0f || seed.temp_pref > 1.0f;
        outside += seed.gut_time < 0.0f || seed.gut_time > 1.0f;
        moved += seed.water_need != parent.water_need;
    }
    CHECK(outside == 0);
    CHECK(moved > 900);
}

/* Too dry or too cold: no seed, whatever its energy. The same plant seeds at
 * once where it fits. */
static void test_plant_needs_energy_and_climate(void)
{
    Settings s = rules();
    Rng r;
    rng_seed(&r, 18);
    int side;
    World w;

    /* Everything right: it seeds. */
    make_world(&w, 9, 9);
    plants_everywhere(&w);
    side = world_idx(&w, 3, 4);
    w.veg[world_idx(&w, 4, 4)] = 1;
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(w.veg[side] == 1);
    world_free(&w);

    /* Too dry: soil water 0 gives a fit of 0. */
    make_world(&w, 9, 9);
    plants_everywhere(&w);
    w.veg[world_idx(&w, 4, 4)] = 1;
    w.water[world_idx(&w, 4, 4)] = 0.0f;
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(w.veg[side] == 0);
    world_free(&w);

    /* Too cold: far below the temperature it prefers. */
    make_world(&w, 9, 9);
    plants_everywhere(&w);
    w.veg[world_idx(&w, 4, 4)] = 1;
    w.temperature[world_idx(&w, 4, 4)] = 0.0f;
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(w.veg[side] == 0);
    world_free(&w);

    /* Fits, but too little energy stored: still no seed. */
    make_world(&w, 9, 9);
    plants_everywhere(&w);
    w.veg[world_idx(&w, 4, 4)] = 1;
    w.plant[world_idx(&w, 4, 4)].energy = s.plant_seed_energy * 0.5f;
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(w.veg[side] == 0);
    world_free(&w);
}

/* Fit: full where it suits, zero on dust, falling with distance from the
 * temperature it prefers, and thirsty plants need wetter soil. */
static void test_plant_fit(void)
{
    Settings s = settings_default();
    Plant thirsty = {.water_need = 1.0f, .temp_pref = 0.5f};
    Plant hardy = {.water_need = 0.0f, .temp_pref = 0.5f};
    CHECK(plant_fit(&thirsty, 1.0f, 0.5f, &s) == 1.0f);
    CHECK(plant_fit(&thirsty, 0.0f, 0.5f, &s) == 0.0f);
    CHECK(plant_fit(&hardy, 0.0f, 0.5f, &s) == 0.0f);
    CHECK(plant_fit(&hardy, 0.3f, 0.5f, &s) == 1.0f);
    CHECK(plant_fit(&thirsty, 0.3f, 0.5f, &s) < 0.5f);
    CHECK(plant_fit(&thirsty, 1.0f, 0.5f + s.plant_temp_width, &s) == 0.0f);
    CHECK(plant_fit(&thirsty, 1.0f, 0.6f, &s) < 1.0f);
}

/* Soil too dry for a plant drains it until it withers and goes. */
static void test_plant_starves_where_dry(void)
{
    Settings s = settings_default();
    s.veg_wither_chance = 0.0f;
    World w;
    make_world(&w, 9, 9);
    int cell = world_idx(&w, 4, 4);
    w.veg[cell] = 1;
    w.plant[cell] = (Plant){.energy = 1.0f, .temp_pref = 0.5f};
    w.water[cell] = 0.0f;
    Rng r;
    rng_seed(&r, 19);
    /* energy / upkeep ticks to run out, then withering. */
    int ticks = (int)(1.0f / s.plant_upkeep) + s.veg_wither_ticks + 5;
    for (int t = 0; t < ticks; t++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    CHECK(w.veg[cell] == 0);
    world_free(&w);
}

/* Only a plant offering fruit makes any, it pays for it, and it stops at the
 * most one plant can hold. */
static void test_fruit_costs_the_tree(void)
{
    Settings s = rules();
    s.veg_grow_chance = 0.0f;
    s.veg_sprout_rate = 0.0f;
    s.plant_gain = 0.0f; /* so the only change in energy is the fruit */
    s.plant_fruit_rate = 0.5f;
    s.plant_fruit_cost = 2.0f;
    s.plant_fruit_max = 2.0f;
    World w;
    make_world(&w, 9, 9);
    int tree = world_idx(&w, 2, 2), bush = world_idx(&w, 6, 6);
    w.veg[tree] = w.veg[bush] = 1;
    w.plant[tree] = healthy_plant();
    w.plant[bush] = healthy_plant();
    w.plant[tree].fruit_share = 1.0f;
    w.plant[bush].fruit_share = 0.0f;
    float before = w.plant[tree].energy;
    Rng r;
    rng_seed(&r, 20);
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(fabsf(w.plant[tree].fruit - 0.5f) < 1e-5f);
    CHECK(fabsf(w.plant[tree].energy - (before - 1.0f)) < 1e-5f);
    CHECK(w.plant[bush].fruit == 0.0f);
    CHECK(w.plant[bush].energy == before);
    for (int t = 0; t < 10; t++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    CHECK(w.plant[tree].fruit <= 2.5f); /* the last step may overshoot once */
    float settled = w.plant[tree].energy;
    flora_step(&w, &r, &s, SEASON_SUMMER);
    CHECK(w.plant[tree].energy == settled);
    world_free(&w);
}

/* One plant in the middle of a 9 by 9 world, the rest bare. */
static int lone_plant(World *w, float fruit_share, float energy)
{
    make_world(w, 9, 9);
    int at = world_idx(w, 4, 4);
    w->veg[at] = 1;
    w->plant[at] = healthy_plant();
    w->plant[at].fruit_share = fruit_share;
    w->plant[at].energy = energy;
    return at;
}

/* Spring: the same plant gains spring_plenty times what it gains in summer,
 * and a fruit tree makes spring_plenty times the fruit. */
static void test_spring_grows_more(void)
{
    Settings s = rules();
    s.veg_sprout_rate = 0.0f;
    s.veg_grow_chance = 0.0f;
    s.plant_fruit_rate = 0.1f;
    s.plant_fruit_cost = 0.0f;
    for (int kind = 0; kind < 2; kind++) {
        float share = kind == 0 ? 0.0f : 1.0f;
        World spring, summer;
        int a = lone_plant(&spring, share, 5.0f);
        lone_plant(&summer, share, 5.0f);
        Rng r1, r2;
        rng_seed(&r1, 3);
        rng_seed(&r2, 3);
        flora_step(&spring, &r1, &s, SEASON_SPRING);
        flora_step(&summer, &r2, &s, SEASON_SUMMER);
        float spring_gain = spring.plant[a].energy - 5.0f;
        float summer_gain = summer.plant[a].energy - 5.0f;
        CHECK(summer_gain > 0.0f);
        CHECK(fabsf(spring_gain - s.spring_plenty * summer_gain) < 1e-5f);
        if (kind == 1) {
            CHECK(fabsf(summer.plant[a].fruit - 0.1f) < 1e-6f);
            CHECK(fabsf(spring.plant[a].fruit - 0.1f * s.spring_plenty) < 1e-6f);
        }
        world_free(&spring);
        world_free(&summer);
    }
}

/* How many cells of the world carry a plant. */
static int planted_count(const World *w)
{
    int count = 0;
    for (int i = 0; i < w->w * w->h; i++) {
        count += w->veg[i] > 0;
    }
    return count;
}

/* Autumn: a fruit tree rests (no income, no fruit, no seeds, no growth) and
 * keeps the fruit it holds; a leaf plant goes on gaining and seeding. */
static void test_autumn_stops_fruit_trees(void)
{
    Settings s = rules();
    s.plant_upkeep = 0.02f;
    s.plant_fruit_rate = 0.1f;
    World w;
    int at = lone_plant(&w, 1.0f, 8.0f);
    w.plant[at].fruit = 2.0f;
    Rng r;
    rng_seed(&r, 4);
    flora_step(&w, &r, &s, SEASON_AUTUMN);
    CHECK(w.plant[at].fruit == 2.0f);
    CHECK(fabsf(w.plant[at].energy - (8.0f - 0.02f)) < 1e-6f);
    CHECK(w.veg[at] == 1);
    CHECK(planted_count(&w) == 1);
    world_free(&w);

    at = lone_plant(&w, 0.0f, 8.0f);
    flora_step(&w, &r, &s, SEASON_AUTUMN);
    CHECK(w.plant[at].energy > 8.0f - 0.02f);
    CHECK(w.veg[at] == 2);
    CHECK(planted_count(&w) > 1); /* sprouts around it */
    world_free(&w);
}

/* Winter: neither kind gains, grows, seeds or fruits, and both pay the
 * resting share of their upkeep. */
static void test_winter_stops_all_plants(void)
{
    Settings s = rules();
    s.plant_upkeep = 0.02f;
    s.plant_rest_upkeep = 0.5f;
    s.plant_fruit_rate = 0.1f;
    for (int kind = 0; kind < 2; kind++) {
        World w;
        int at = lone_plant(&w, kind == 0 ? 0.0f : 1.0f, 8.0f);
        Rng r;
        rng_seed(&r, 5);
        flora_step(&w, &r, &s, SEASON_WINTER);
        CHECK(fabsf(w.plant[at].energy - (8.0f - 0.01f)) < 1e-6f);
        CHECK(w.plant[at].fruit == 0.0f);
        CHECK(w.veg[at] == 1);
        CHECK(planted_count(&w) == 1);
        world_free(&w);
    }
    /* A resting plant with nothing stored stays at 0, never below. */
    World w;
    int at = lone_plant(&w, 1.0f, 0.001f);
    Rng r;
    rng_seed(&r, 5);
    flora_step(&w, &r, &s, SEASON_WINTER);
    CHECK(w.plant[at].energy == 0.0f);
    world_free(&w);
}

/* The season changes what plants do, never how many numbers flora_step draws.
 * Every cell is planted, so no sprout can add a seed's own draws (those come
 * only when a sprout really happens, in any season), and half the plants are
 * fruit trees so that autumn's rule is in play too. */
static void test_seasons_keep_three_draws(void)
{
    Settings s = rules();
    s.veg_wither_chance = 0.01f;
    Rng after[SEASON_COUNT];
    for (int season = 0; season < SEASON_COUNT; season++) {
        World w;
        make_world(&w, 10, 10);
        plants_everywhere(&w);
        for (int i = 0; i < 100; i++) {
            w.veg[i] = 1;
            w.plant[i].fruit_share = i % 2 == 0 ? 1.0f : 0.0f;
        }
        rng_seed(&after[season], 9);
        flora_step(&w, &after[season], &s, season);
        world_free(&w);
    }
    /* A struct cannot be compared with ==; the generator is four uint64_t. */
    for (int season = 0; season < SEASON_COUNT; season++) {
        for (int k = 0; k < 4; k++) {
            CHECK(after[season].s[k] == after[SEASON_SUMMER].s[k]);
        }
    }
}

/* Spec section 12: an area that stops getting rain loses its plants, and when
 * rain returns they come back from its edges. The water layer is set by hand;
 * test_climate already shows rain drying and wetting it. */
static void test_desert_and_back(void)
{
    World w;
    make_world(&w, 60, 12);
    Settings s = settings_default();
    s.plant_mutation = 0.0f;
    s.veg_capacity = 40.0f;
    s.veg_sprout_rate = 0.2f;
    plants_everywhere(&w);
    for (int i = 0; i < 60 * 12; i++) {
        w.veg[i] = 1;
    }
    Rng r;
    rng_seed(&r, 21);
    /* The middle of the strip dries out; columns 0..9 and 50..59 stay wet. */
    for (int y = 0; y < 12; y++) {
        for (int x = 10; x < 50; x++) {
            w.water[world_idx(&w, x, y)] = 0.0f;
        }
    }
    for (int t = 0; t < 2000; t++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    int dry_planted = 0;
    int wet_planted = 0;
    for (int y = 0; y < 12; y++) {
        for (int x = 0; x < 60; x++) {
            bool dry = x >= 10 && x < 50;
            /* A sprout on the dry edge comes from the wet side and is dying,
             * so only the desert's inside has to be empty. */
            dry_planted += x >= 15 && x < 45 && w.veg[world_idx(&w, x, y)] > 0;
            wet_planted += !dry && w.veg[world_idx(&w, x, y)] > 0;
        }
    }
    CHECK(dry_planted == 0);
    CHECK(wet_planted > 0);

    /* Rain returns. Soon after, the plants nearest the edge are back, and the
     * middle still is not; given time, the middle is planted too. */
    for (int y = 0; y < 12; y++) {
        for (int x = 10; x < 50; x++) {
            w.water[world_idx(&w, x, y)] = 1.0f;
        }
    }
    int near_edge = 0;
    int middle = 0;
    for (int t = 0; t < 40; t++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    for (int y = 0; y < 12; y++) {
        for (int x = 15; x < 45; x++) {
            bool centre = x >= 25 && x < 35;
            middle += centre && w.veg[world_idx(&w, x, y)] > 0;
            near_edge += x < 20 && w.veg[world_idx(&w, x, y)] > 0;
        }
    }
    CHECK(near_edge > middle);
    for (int t = 0; t < 3000; t++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    middle = 0;
    for (int y = 0; y < 12; y++) {
        for (int x = 25; x < 35; x++) {
            middle += w.veg[world_idx(&w, x, y)] > 0;
        }
    }
    CHECK(middle > 0);
    world_free(&w);
}

/* Bare soil with no plant anywhere stays bare: a plant comes only from a
 * plant. Everything that could help is turned up. */
static void test_no_sprout_from_nothing(void)
{
    World w;
    make_world(&w, 16, 16);
    Settings s = rules();
    Rng r;
    rng_seed(&r, 18);
    for (int t = 0; t < 10000; t++) {
        flora_step(&w, &r, &s, SEASON_SUMMER);
    }
    int planted = 0;
    for (int i = 0; i < w.w * w.h; i++) {
        planted += w.veg[i] > 0;
    }
    CHECK(planted == 0);
    world_free(&w);
}

static void test_carrion_merges(void)
{
    World w;
    make_world(&w, 4, 4);
    int i = world_idx(&w, 1, 1);
    carrion_add(&w, i, 10.0f, 3.0f);
    carrion_add(&w, i, 10.0f, 2.0f);
    CHECK(fabsf(w.carrion[i] - 20.0f) < 1e-4f);
    CHECK(fabsf(w.carrion_level[i] - 2.5f) < 1e-4f);
    carrion_add(&w, i, 0.0f, 9.0f); /* nothing: level unchanged */
    CHECK(fabsf(w.carrion_level[i] - 2.5f) < 1e-4f);
    world_free(&w);
}

static void test_carrion_rots_into_soil(void)
{
    World w;
    make_world(&w, 4, 4);
    Settings s = settings_default();
    s.carrion_decay = 0.1f;
    s.carrion_to_fertility = 0.001f;
    s.fertility_max = 1.0f;
    int i = world_idx(&w, 2, 2);
    w.fertility[i] = 0.5f;
    carrion_add(&w, i, 100.0f, 2.0f);
    carrion_step(&w, &s);
    CHECK(fabsf(w.carrion[i] - 90.0f) < 1e-4f);
    CHECK(fabsf(w.fertility[i] - (0.5f + 10.0f * 0.001f)) < 1e-4f);

    /* Never above fertility_max. */
    w.fertility[i] = 0.999f;
    carrion_step(&w, &s);
    CHECK(w.fertility[i] == s.fertility_max);
    world_free(&w);
}

static void test_carrion_disappears(void)
{
    World w;
    make_world(&w, 4, 4);
    Settings s = settings_default();
    s.carrion_decay = 0.5f;
    int i = world_idx(&w, 0, 0);
    carrion_add(&w, i, 0.015f, 2.0f);
    carrion_step(&w, &s); /* 0.0075 left: below CARRION_MIN */
    CHECK(w.carrion[i] == 0.0f);
    CHECK(w.carrion_level[i] == 0.0f);
    world_free(&w);
}

static void test_scent_spreads_and_fades(void)
{
    World w;
    make_world(&w, 21, 21);
    Settings s = settings_default();
    int source = world_idx(&w, 10, 10);
    carrion_add(&w, source, 50.0f, 2.0f);
    for (int t = 0; t < 50; t++) {
        scent_step(&w, &s);
    }
    float at = w.scent[source];
    float one = w.scent[world_idx(&w, 11, 10)];
    float three = w.scent[world_idx(&w, 13, 10)];
    CHECK(at > one && one > three && three > 0.0f);
    /* The same in every direction. */
    CHECK(fabsf(w.scent[world_idx(&w, 9, 10)] - one) < 1e-5f);
    CHECK(fabsf(w.scent[world_idx(&w, 10, 11)] - one) < 1e-5f);

    /* With the carrion gone, the scent fades away. */
    w.carrion[source] = 0.0f;
    for (int t = 0; t < 3000; t++) {
        scent_step(&w, &s);
    }
    CHECK(w.scent[source] == 0.0f);
    world_free(&w);
}

int main(void)
{
    test_lone_cell_grows_to_max();
    test_water_and_rock_stay_bare();
    test_barren_world_stays_empty();
    test_capacity_blocks_sprouting();
    test_planted_cells_cap();
    test_capacity_scales_with_fertility();
    test_over_capacity_shrinks();
    test_tiny_world_is_safe();
    test_plant_withers_then_disappears();
    test_no_wither_chance_lives_on();
    test_sprout_starts_young();
    test_growth_follows_its_chance();
    test_sprouting_follows_its_rate();
    test_sprout_copies_its_parent();
    test_sprout_picks_one_neighbour();
    test_seed_inherits_and_mutates();
    test_plant_needs_energy_and_climate();
    test_plant_fit();
    test_plant_starves_where_dry();
    test_fruit_costs_the_tree();
    test_spring_grows_more();
    test_autumn_stops_fruit_trees();
    test_winter_stops_all_plants();
    test_seasons_keep_three_draws();
    test_desert_and_back();
    test_no_sprout_from_nothing();
    test_carrion_merges();
    test_carrion_rots_into_soil();
    test_carrion_disappears();
    test_scent_spreads_and_fades();
    TEST_SUMMARY();
}
