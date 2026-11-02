#include "test.h"

#include <math.h>   /* fabsf */
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

/* Default settings with the starting rule's numbers replaced, and no
 * seeding from nothing and no mutation, so every change in these tests
 * comes from the rule itself. The rule must also be laid on the cells:
 * see rule_everywhere. */
static Settings rule(int birth_lo, int birth_hi, int crowd_max)
{
    Settings s = settings_default();
    s.veg_birth_lo = birth_lo;
    s.veg_birth_hi = birth_hi;
    s.veg_crowd_max = crowd_max;
    s.veg_seed_chance = 0.0f;
    s.veg_mutation = 0.0f;
    s.veg_vigour_ageing = 0.0f; /* every rule matures at veg_mature_age */
    /* Out of reach: these tests are about the neighbour rule alone. */
    s.veg_capacity = 1000.0f;
    s.veg_wither_chance = 0.0f; /* plants never wither */
    /* Sprouting and growth always happen when the rule allows them. */
    s.veg_sprout_rate = 1.0f;
    s.veg_grow_chance = 1.0f;
    return s;
}

/* Every cell carries the starting rule of s: what flora_step reads for
 * a planted cell, and what an empty cell inherits from its neighbours. */
static void rule_everywhere(World *w, const Settings *s)
{
    uint8_t packed =
        veg_rule_pack(s->veg_birth_lo, s->veg_birth_hi, s->veg_crowd_max);
    memset(w->veg_rule, packed, (size_t)(w->w * w->h));
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
    rule_everywhere(&w, &s);
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
    rule_everywhere(&w, &s);
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
    /* Birth range 4..4: no cell around the block has exactly four
     * planted neighbours (at most three), so nothing sprouts there. */
    Settings s = rule(4, 4, 3);
    rule_everywhere(&w, &s);
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
    Settings s = rule(1, 4, 7);
    s.veg_seed_chance = 1.0f; /* even forced sprouting must not reach them */
    rule_everywhere(&w, &s);
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
    rule_everywhere(&w, &s);
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
    Settings s = rule(1, 4, 7);
    s.veg_reach = 2;
    s.veg_capacity = 20.0f;
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 6);
    ring(&w, 5, 5, 2, 1);
    clear_corners(&w, 5, 5);
    flora_step(&w, &r, &s);
    CHECK(veg_at(&w, 5, 5) == 0);

    /* Same neighbourhood with room to spare: it sprouts. */
    world_free(&w);
    make_world(&w, 11, 11);
    rule_everywhere(&w, &s);
    s.veg_capacity = 30.0f;
    ring(&w, 5, 5, 2, 1);
    clear_corners(&w, 5, 5);
    flora_step(&w, &r, &s);
    CHECK(veg_at(&w, 5, 5) == 1);
    world_free(&w);
}

static void test_planted_cells_cap(void)
{
    /* Eight planted cells around (5, 5). With birth range 1..4 the cells
     * around the ring (1 to 3 planted neighbours each) would sprout. */
    World w;
    Settings s = rule(1, 4, 7);
    Rng r;
    rng_seed(&r, 6);

    /* A cap of 8 is already reached: nothing sprouts, and the eight
     * plants stay. */
    make_world(&w, 11, 11);
    rule_everywhere(&w, &s);
    s.veg_max_cells = 8;
    ring(&w, 5, 5, 1, 1);
    flora_step(&w, &r, &s);
    CHECK(planted_cells(&w) == 8);
    world_free(&w);

    /* A cap of 9 is not reached at the start of the tick, so sprouting
     * goes ahead; the count is taken once per tick, so it can overshoot. */
    make_world(&w, 11, 11);
    rule_everywhere(&w, &s);
    s.veg_max_cells = 9;
    ring(&w, 5, 5, 1, 1);
    flora_step(&w, &r, &s);
    CHECK(planted_cells(&w) > 9);
    world_free(&w);

    /* 0 means no cap. */
    make_world(&w, 11, 11);
    rule_everywhere(&w, &s);
    s.veg_max_cells = 0;
    ring(&w, 5, 5, 1, 1);
    flora_step(&w, &r, &s);
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
    Settings s = rule(1, 4, 7);
    s.veg_reach = 2;
    s.veg_capacity = 40.0f;
    rule_everywhere(&w, &s);
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
    /* A planted centre with room among its touching neighbours (7 of
     * them planted, crowd limit 7) but m = 23 * 2 = 46 above capacity 30:
     * it thins by one per step instead of growing. */
    World w;
    make_world(&w, 11, 11);
    Settings s = rule(4, 4, 7);
    s.veg_reach = 2;
    s.veg_capacity = 30.0f;
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 8);
    ring(&w, 5, 5, 2, 2);
    w.veg[world_idx(&w, 4, 4)] = 0;
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
    Settings s = rule(1, 4, 7);
    s.veg_reach = 3;
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 9);
    w.veg[0] = 1;
    for (int i = 0; i < 10; i++) {
        flora_step(&w, &r, &s);
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
    Settings s = rule(4, 4, 7);
    s.veg_mature_age = 2;
    s.veg_wither_chance = 1.0f;
    s.veg_wither_ticks = 3;
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 10);
    int i = world_idx(&w, 3, 3);
    w.veg[i] = 1;

    flora_step(&w, &r, &s); /* age 1: not mature yet, grows */
    CHECK(w.veg[i] == 2 && w.veg_age[i] == 1 && w.veg_wither[i] == 0);
    flora_step(&w, &r, &s); /* age 2: mature, starts withering */
    CHECK(w.veg[i] == 2 && w.veg_wither[i] == 1);
    flora_step(&w, &r, &s); /* withering: no growth */
    flora_step(&w, &r, &s);
    CHECK(w.veg[i] == 2 && w.veg_wither[i] == 3);
    flora_step(&w, &r, &s); /* withering is over: gone */
    CHECK(w.veg[i] == 0 && w.veg_age[i] == 0 && w.veg_wither[i] == 0);
    world_free(&w);
}

static void test_no_wither_chance_lives_on(void)
{
    World w;
    make_world(&w, 7, 7);
    Settings s = rule(4, 4, 7);
    s.veg_mature_age = 1;
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 11);
    int i = world_idx(&w, 3, 3);
    w.veg[i] = 1;
    for (int step = 0; step < 300; step++) {
        flora_step(&w, &r, &s);
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
    Settings s = rule(3, 3, 3);
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 12);
    w.veg[world_idx(&w, 4, 3)] = 1;
    w.veg[world_idx(&w, 4, 4)] = 1;
    w.veg[world_idx(&w, 4, 5)] = 1;
    int side = world_idx(&w, 3, 4);
    w.veg_age[side] = 500;
    w.veg_wither[side] = 7;
    flora_step(&w, &r, &s);
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
    Settings s = rule(4, 4, 7);
    s.veg_grow_chance = 0.25f;
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 13);
    for (int y = 0; y < w.h; y++) {
        for (int x = 0; x < w.w; x += 2) {
            w.veg[world_idx(&w, x, y)] = 1;
        }
    }
    flora_step(&w, &r, &s);
    int grown = 0;
    for (int i = 0; i < w.w * w.h; i++) {
        grown += w.veg[i] == 2;
    }
    double share = (double)grown / (w.w * w.h / 2);
    CHECK(share > 0.22 && share < 0.28);

    /* A chance of 0: nothing ever thickens. */
    s.veg_grow_chance = 0.0f;
    for (int step = 0; step < 50; step++) {
        flora_step(&w, &r, &s);
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
    Settings s = rule(4, 4, 7);
    s.veg_sprout_rate = 0.25f;
    rule_everywhere(&w, &s);
    Rng r;
    rng_seed(&r, 14);
    for (int y = 0; y < w.h; y++) {
        for (int x = 0; x < w.w; x++) {
            w.veg[world_idx(&w, x, y)] = (x + y) % 2 == 0 ? 1 : 0;
        }
    }
    flora_step(&w, &r, &s);
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

static void test_rule_pack_round_trip(void)
{
    /* Every byte with bit 7 clear is a rule. Unpacking it and packing
     * the three numbers again must give the same byte back, and the
     * numbers must be inside their documented ranges. */
    int bad = 0;
    for (int b = 0; b <= (int)VEG_RULE_USED_MASK; b++) {
        uint8_t rule_byte = (uint8_t)b;
        int lo = veg_rule_birth_lo(rule_byte);
        int hi = veg_rule_birth_hi(rule_byte);
        int crowd = veg_rule_crowd_max(rule_byte);
        if (lo < 1 || lo > 4 || hi < lo || hi > lo + 3 || crowd < 0 ||
            crowd > 7 || veg_rule_pack(lo, hi, crowd) != rule_byte) {
            bad++;
        }
    }
    CHECK(bad == 0);
    /* The lesson 4 defaults: born with 3 or 4 neighbours, crowd limit 3.
     * Low 3 is stored as 2 (binary 10), width 1 (01), crowd 3 (011):
     * 0 011 01 10 is 0x36. */
    CHECK(veg_rule_pack(3, 4, 3) == 0x36);
    /* Out of range values are clamped, not wrapped into other bits. */
    CHECK(veg_rule_pack(0, 9, 9) == veg_rule_pack(1, 4, 7));
    CHECK(veg_rule_pack(9, 2, -1) == veg_rule_pack(4, 4, 0));
}

/* Three plants in a column at (4, 3), (4, 4), (4, 5) carrying the given
 * rules; the cells beside the middle one, (3, 4) and (5, 4), have three
 * planted neighbours. */
static void column_of_three(World *w, uint8_t top, uint8_t middle,
                            uint8_t bottom)
{
    int a = world_idx(w, 4, 3), b = world_idx(w, 4, 4), c = world_idx(w, 4, 5);
    w->veg[a] = w->veg[b] = w->veg[c] = 1;
    w->veg_rule[a] = top;
    w->veg_rule[b] = middle;
    w->veg_rule[c] = bottom;
}

static void test_sprout_copies_its_parent(void)
{
    /* All three neighbours carry rule R (born with 3, crowd limit 5,
     * unlike the default): with no mutation, the sprouts beside the
     * middle carry R too. */
    World w;
    make_world(&w, 9, 9);
    Settings s = rule(3, 3, 3);
    uint8_t parent_rule = veg_rule_pack(3, 3, 5);
    Rng r;
    rng_seed(&r, 15);
    column_of_three(&w, parent_rule, parent_rule, parent_rule);
    flora_step(&w, &r, &s);
    CHECK(veg_at(&w, 3, 4) == 1 && veg_at(&w, 5, 4) == 1);
    CHECK(w.veg_rule[world_idx(&w, 3, 4)] == parent_rule);
    CHECK(w.veg_rule[world_idx(&w, 5, 4)] == parent_rule);
    world_free(&w);
}

static void test_sprout_picks_one_neighbour(void)
{
    /* Three different rules, all born with exactly 3 neighbours. Over
     * many tries, each sprout copies one of the three, and each of the
     * three is picked sometimes. */
    uint8_t rules[3] = {veg_rule_pack(3, 3, 1), veg_rule_pack(3, 3, 4),
                        veg_rule_pack(3, 6, 7)};
    int picked[3] = {0, 0, 0};
    int foreign = 0;
    Settings s = rule(3, 3, 3);
    Rng r;
    rng_seed(&r, 16);
    for (int attempt = 0; attempt < 60; attempt++) {
        World w;
        make_world(&w, 9, 9);
        column_of_three(&w, rules[0], rules[1], rules[2]);
        flora_step(&w, &r, &s);
        uint8_t got = w.veg_rule[world_idx(&w, 3, 4)];
        int known = 0;
        for (int k = 0; k < 3; k++) {
            if (got == rules[k]) {
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

/* How many bits are set in v. */
static int bits_set(unsigned v)
{
    int count = 0;
    while (v != 0) {
        count += (int)(v & 1u);
        v >>= 1;
    }
    return count;
}

static void test_mutation_flips_one_bit(void)
{
    /* With mutation 1, every sprout's rule differs from its parent's in
     * exactly one of the seven bits in use: XOR keeps the bits that
     * differ, and exactly one must be set, never bit 7. Across many
     * sprouts, every one of the seven bits gets flipped at some point. */
    Settings s = rule(3, 3, 3);
    s.veg_mutation = 1.0f;
    uint8_t parent_rule = veg_rule_pack(3, 3, 3);
    Rng r;
    rng_seed(&r, 17);
    int wrong = 0;
    unsigned flipped = 0;
    for (int attempt = 0; attempt < 100; attempt++) {
        World w;
        make_world(&w, 9, 9);
        column_of_three(&w, parent_rule, parent_rule, parent_rule);
        flora_step(&w, &r, &s);
        unsigned diff =
            (unsigned)(w.veg_rule[world_idx(&w, 3, 4)] ^ parent_rule);
        if (veg_at(&w, 3, 4) != 1 || bits_set(diff) != 1 ||
            (diff & ~VEG_RULE_USED_MASK) != 0) {
            wrong++;
        }
        flipped |= diff;
        world_free(&w);
    }
    CHECK(wrong == 0);
    CHECK(flipped == VEG_RULE_USED_MASK);
}

static void test_lone_sprouts_get_random_rules(void)
{
    /* Seeding from nothing is certain and nothing is planted: every
     * cell sprouts with no planted neighbour, so with a random rule.
     * 4,096 random picks out of 128 rules should leave none unused, and
     * bit 7 must stay clear. */
    World w;
    make_world(&w, 64, 64);
    Settings s = rule(3, 3, 3);
    s.veg_seed_chance = 1.0f;
    Rng r;
    rng_seed(&r, 18);
    flora_step(&w, &r, &s);
    int seen[VEG_RULE_USED_MASK + 1] = {0};
    int high_bit = 0;
    int planted = 0;
    for (int i = 0; i < w.w * w.h; i++) {
        planted += w.veg[i] > 0;
        high_bit += (w.veg_rule[i] & 0x80u) != 0;
        seen[w.veg_rule[i] & VEG_RULE_USED_MASK] = 1;
    }
    int distinct = 0;
    for (int k = 0; k <= (int)VEG_RULE_USED_MASK; k++) {
        distinct += seen[k];
    }
    CHECK(planted == w.w * w.h);
    CHECK(high_bit == 0);
    CHECK(distinct == (int)VEG_RULE_USED_MASK + 1);
    world_free(&w);
}

static void test_plant_reads_its_own_rule(void)
{
    /* Two touching plants, each with one planted neighbour (the other).
     * The left one's crowd limit is 0, so one neighbour is too many and
     * it shrinks; the right one's is 7, so it grows. */
    World w;
    make_world(&w, 9, 9);
    Settings s = rule(4, 4, 7);
    Rng r;
    rng_seed(&r, 19);
    int left = world_idx(&w, 3, 4), right = world_idx(&w, 4, 4);
    w.veg[left] = 2;
    w.veg[right] = 2;
    w.veg_rule[left] = veg_rule_pack(4, 4, 0);
    w.veg_rule[right] = veg_rule_pack(4, 4, 7);
    flora_step(&w, &r, &s);
    CHECK(w.veg[left] == 1);
    CHECK(w.veg[right] == 3);
    world_free(&w);
}

static void test_rule_vigour(void)
{
    /* The most modest rule is 0, the most aggressive 1, the starting
     * rule 5 / 13, and every rule lies in between. */
    CHECK(veg_rule_vigour(veg_rule_pack(4, 4, 0)) == 0.0f);
    CHECK(veg_rule_vigour(veg_rule_pack(1, 4, 7)) == 1.0f);
    CHECK(fabsf(veg_rule_vigour(veg_rule_pack(3, 4, 3)) - 5.0f / 13.0f) <
          1e-6f);
    int outside = 0;
    for (int b = 0; b <= (int)VEG_RULE_USED_MASK; b++) {
        float v = veg_rule_vigour((uint8_t)b);
        outside += v < 0.0f || v > 1.0f;
    }
    CHECK(outside == 0);
}

static void test_vigorous_plants_die_young(void)
{
    /* Mature age 10, ageing cost 0.5, withering certain once mature. A
     * plant with the most aggressive rule (vigour 1) matures at
     * 10 * (1 - 0.5) = 5; one with the most modest rule (vigour 0) at
     * 10. A sprout rate of 0 keeps the cells around them empty, so the
     * two plants stay alone. */
    World w;
    make_world(&w, 16, 16);
    Settings s = rule(4, 4, 0);
    s.veg_sprout_rate = 0.0f;
    s.veg_mature_age = 10;
    s.veg_wither_chance = 1.0f;
    s.veg_vigour_ageing = 0.5f;
    Rng r;
    rng_seed(&r, 20);
    int fast = world_idx(&w, 3, 3), slow = world_idx(&w, 11, 11);
    w.veg[fast] = 1;
    w.veg[slow] = 1;
    w.veg_rule[fast] = veg_rule_pack(1, 4, 7);
    w.veg_rule[slow] = veg_rule_pack(4, 4, 0);
    for (int step = 0; step < 5; step++) {
        flora_step(&w, &r, &s);
    }
    CHECK(w.veg_wither[fast] == 1); /* started at age 5 */
    CHECK(w.veg_wither[slow] == 0);
    for (int step = 0; step < 5; step++) {
        flora_step(&w, &r, &s);
    }
    CHECK(w.veg_wither[slow] == 1); /* started at age 10 */

    /* With the cost off, both mature at 10. */
    world_free(&w);
    make_world(&w, 16, 16);
    s.veg_vigour_ageing = 0.0f;
    w.veg[fast] = 1;
    w.veg_rule[fast] = veg_rule_pack(1, 4, 7);
    for (int step = 0; step < 9; step++) {
        flora_step(&w, &r, &s);
    }
    CHECK(w.veg_wither[fast] == 0);
    flora_step(&w, &r, &s);
    CHECK(w.veg_wither[fast] == 1);
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
    test_line_of_three();
    test_lone_cell_grows_to_max();
    test_crowded_cell_shrinks();
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
    test_rule_pack_round_trip();
    test_sprout_copies_its_parent();
    test_sprout_picks_one_neighbour();
    test_mutation_flips_one_bit();
    test_lone_sprouts_get_random_rules();
    test_plant_reads_its_own_rule();
    test_rule_vigour();
    test_vigorous_plants_die_young();
    test_carrion_merges();
    test_carrion_rots_into_soil();
    test_carrion_disappears();
    test_scent_spreads_and_fades();
    TEST_SUMMARY();
}
