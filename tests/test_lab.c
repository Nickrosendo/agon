#include "test.h"

#include <stdint.h>

#include "canvas.h"
#include "lab.h"
#include "palette.h"
#include "sprite.h"

/* One picture cache for all the tests: building it is the slow part. */
static SpriteCache sprites;

/* Every species shows in exactly one cell, in the row of its role. */
static void test_lab_shows_every_species_once(void)
{
    int seen[SPECIES_COUNT] = {0};
    for (int role = 0; role < ROLE_COUNT; role++) {
        for (int col = 0; col < LAB_COLUMNS; col++) {
            int s = lab_species_at(role, col);
            if (s >= 0) {
                CHECK(s < SPECIES_COUNT);
                CHECK(SPECIES[s].role == role);
                seen[s]++;
            }
        }
    }
    for (int s = 0; s < SPECIES_COUNT; s++) {
        CHECK(seen[s] == 1);
    }
    CHECK(lab_species_at(-1, 0) == -1);
    CHECK(lab_species_at(ROLE_COUNT, 0) == -1);
    CHECK(lab_species_at(0, -1) == -1);
    CHECK(lab_species_at(0, LAB_COLUMNS) == -1);
}

#define GUARD 0xFF123456u

/* Guard colour all over, the area drawn, and the area's surroundings still
 * guard colour. */
static void test_lab_stays_in_its_area(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 700, 520));
    canvas_fill(&c, GUARD);
    int ax = 30, ay = 20, aw = 600, ah = 480;
    lab_draw(&sprites, &c, ACTION_WALK, 1234, ax, ay, aw, ah);
    int outside_changed = 0, inside_changed = 0;
    for (int y = 0; y < c.h; y++) {
        for (int x = 0; x < c.w; x++) {
            bool inside = x >= ax && x < ax + aw && y >= ay && y < ay + ah;
            bool changed = c.px[y * c.w + x] != GUARD;
            if (!inside && changed) {
                outside_changed++;
            }
            if (inside && changed) {
                inside_changed++;
            }
        }
    }
    CHECK(outside_changed == 0);
    CHECK(inside_changed > aw * ah / 2);
    canvas_free(&c);
}

/* Each cell has pixels that are neither sky nor ground: an animal. Checked in
 * the detailed scale 1 and the scale 2 areas. */
static bool is_scenery(uint32_t p)
{
    return p == canvas_rgb(22, 30, 34) || p == canvas_rgb(44, 70, 41) ||
           p == canvas_rgb(54, 90, 49) || p == canvas_rgb(65, 105, 58);
}

static void check_cells_not_empty(int w, int h, int action, uint64_t body_ms)
{
    Canvas c;
    CHECK(canvas_init(&c, w, h));
    lab_draw(&sprites, &c, action, body_ms, 0, 0, w, h);
    for (int role = 0; role < ROLE_COUNT; role++) {
        for (int col = 0; col < LAB_COLUMNS; col++) {
            if (lab_species_at(role, col) < 0) {
                continue;
            }
            int bx, by, bw, bh;
            CHECK(lab_cell_box(&sprites, action, w, h, role, col, &bx, &by, &bw, &bh) > 0);
            int animal = 0;
            for (int y = by; y < by + bh; y++) {
                for (int x = bx; x < bx + bw; x++) {
                    if (!is_scenery(c.px[y * c.w + x])) {
                        animal++;
                    }
                }
            }
            CHECK(animal > 100);
        }
    }
    canvas_free(&c);
}

static void test_lab_cells_not_empty(void)
{
    check_cells_not_empty(1112, 688, ACTION_REST, 0);  /* the window's world area */
    check_cells_not_empty(1400, 1000, ACTION_REST, 0); /* scale 2 */
}

/* The number keys choose six different actions, and every one of them draws
 * an animal in every cell, at several moments of its cycle. */
static void test_every_action_draws(void)
{
    bool seen[ACTION_COUNT] = {false};
    for (int key = 1; key <= LAB_KEYS; key++) {
        int action = lab_action_of_key(key);
        CHECK(action >= 0 && action < ACTION_COUNT);
        if (action < 0 || action >= ACTION_COUNT) {
            continue;
        }
        CHECK(!seen[action]);
        seen[action] = true;
        for (uint64_t ms = 0; ms < 2000; ms += 700) {
            check_cells_not_empty(1112, 688, action, ms);
        }
    }
    CHECK(lab_action_of_key(0) == -1 && lab_action_of_key(LAB_KEYS + 1) == -1);
}

/* How many pixels of the cell at (role, col) are not scenery. */
static int animal_pixels(const Canvas *c, int action, int role, int col)
{
    int bx, by, bw, bh;
    CHECK(lab_cell_box(&sprites, action, c->w, c->h, role, col, &bx, &by, &bw, &bh) > 0);
    int animal = 0;
    for (int y = by; y < by + bh; y++) {
        for (int x = bx; x < bx + bw; x++) {
            animal += !is_scenery(c->px[y * c->w + x]);
        }
    }
    return animal;
}

/* On key 5 each hunter attacks a deer that faces it: its cell holds two
 * animals, the others still one. */
static void test_hunters_attack_a_deer(void)
{
    int bite = lab_action_of_key(5);
    CHECK(bite == ACTION_BITE);
    Canvas rest, attack;
    CHECK(canvas_init(&rest, 1112, 688));
    CHECK(canvas_init(&attack, 1112, 688));
    lab_draw(&sprites, &rest, ACTION_REST, 0, 0, 0, 1112, 688);
    lab_draw(&sprites, &attack, bite, 0, 0, 0, 1112, 688);
    for (int col = 0; col < LAB_COLUMNS; col++) {
        if (lab_species_at(ROLE_HUNTER, col) < 0) {
            continue;
        }
        int alone = animal_pixels(&rest, ACTION_REST, ROLE_HUNTER, col);
        int pair = animal_pixels(&attack, bite, ROLE_HUNTER, col);
        CHECK(pair > alone * 3 / 2);
        if (pair <= alone * 3 / 2) {
            printf("  hunter column %d: alone %d, with the deer %d\n", col, alone, pair);
        }
    }
    canvas_free(&rest);
    canvas_free(&attack);
}

/* How many pixels of the canvas pass `is`. */
static int pixels_where(const Canvas *c, bool (*is)(uint32_t))
{
    int n = 0;
    for (int i = 0; i < c->w * c->h; i++) {
        n += is(c->px[i]);
    }
    return n;
}

static bool is_z(uint32_t px) { return px == canvas_rgb(220, 230, 255); }
static bool is_leaf(uint32_t px) { return px == canvas_rgb(130, 210, 100); }
static bool is_blood(uint32_t px)
{
    uint32_t r = (px >> 16) & 0xFFu, g = (px >> 8) & 0xFFu, b = px & 0xFFu;
    return r >= 176u && r <= 216u && g == 28u && b == 40u;
}

/* Over three seconds the lab's sleepers send up z's, its grazers leaves, and
 * its hunters' bites blood (v9:689-693); resting, none of them. */
static void test_lab_particles(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 1112, 688));
    int zs = 0, leaves = 0, blood = 0, resting = 0;
    for (uint64_t ms = 0; ms < 3000u; ms += 50u) {
        lab_draw(&sprites, &c, ACTION_SLEEP, ms, 0, 0, 1112, 688);
        zs += pixels_where(&c, is_z) > 0;
        lab_draw(&sprites, &c, ACTION_GRAZE, ms, 0, 0, 1112, 688);
        leaves += pixels_where(&c, is_leaf) > 0;
        lab_draw(&sprites, &c, ACTION_BITE, ms, 0, 0, 1112, 688);
        blood += pixels_where(&c, is_blood) > 0;
        lab_draw(&sprites, &c, ACTION_REST, ms, 0, 0, 1112, 688);
        resting += pixels_where(&c, is_z) + pixels_where(&c, is_leaf) + pixels_where(&c, is_blood);
    }
    CHECK(zs > 30);     /* a Z lives 1.6 s of every 1.2 s, offset per animal */
    CHECK(leaves > 30); /* a leaf every 0.15 s */
    CHECK(blood > 5);   /* 0.75 s after each bite, once per 1.8 s cycle */
    CHECK(resting == 0);
    if (zs <= 30 || leaves <= 30 || blood <= 5) {
        printf("  frames with z's %d, leaves %d, blood %d of 60\n", zs, leaves, blood);
    }
    canvas_free(&c);
}

/* The window's world area at 1280x720 (the panel beside it) is big enough for
 * scale 2; a small area falls back to 1; a very large one may take 3. */
static void test_lab_scale_at_window_size(void)
{
    int bx, by, bw, bh;
    CHECK(lab_cell_box(&sprites, ACTION_REST, 1112, 688, 0, 0, &bx, &by, &bw, &bh) >= 2);
    CHECK(lab_cell_box(&sprites, ACTION_REST, 1280, 720, 0, 0, &bx, &by, &bw, &bh) >= 2);
    CHECK(lab_cell_box(&sprites, ACTION_REST, 600, 400, 0, 0, &bx, &by, &bw, &bh) == 1);
    CHECK(lab_cell_box(&sprites, ACTION_REST, 20, 20, 0, 0, &bx, &by, &bw, &bh) == 0);
}

/* An area from nothing to 40 pixels each way, on a guard canvas: nothing outside
 * the area changes, and an empty area changes nothing at all. */
static void test_lab_tiny_area_is_safe(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 120, 120));
    for (int w = 0; w <= 40; w += 5) {
        for (int h = 0; h <= 40; h += 5) {
            canvas_fill(&c, GUARD);
            lab_draw(&sprites, &c, ACTION_SLEEP, 99, 40, 40, w, h);
            int changed_outside = 0, changed_any = 0;
            for (int y = 0; y < c.h; y++) {
                for (int x = 0; x < c.w; x++) {
                    bool inside = x >= 40 && x < 40 + w && y >= 40 && y < 40 + h;
                    if (c.px[y * c.w + x] != GUARD) {
                        changed_any++;
                        if (!inside) {
                            changed_outside++;
                        }
                    }
                }
            }
            CHECK(changed_outside == 0);
            if (w == 0 || h == 0) {
                CHECK(changed_any == 0);
            }
        }
    }
    /* An area hanging off the canvas's corner is clipped, not a crash. */
    lab_draw(&sprites, &c, ACTION_REST, 0, -30, -30, 200, 200);
    lab_draw(&sprites, &c, 99, 0, 100, 100, 200, 200);
    canvas_free(&c);
}

/* FNV-1a, a simple hash: each pixel's bytes are mixed into a running number. */
static uint64_t hash_canvas(const Canvas *c)
{
    uint64_t h = 14695981039346656037u;
    for (int i = 0; i < c->w * c->h; i++) {
        for (int shift = 0; shift < 32; shift += 8) {
            h ^= (c->px[i] >> shift) & 0xFFu;
            h *= 1099511628211u;
        }
    }
    return h;
}

/* The lab's picture at a fixed window size, action and body clock. */
static void test_lab_pixels(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 1112, 688));
    canvas_fill(&c, canvas_rgb(255, 0, 255));
    lab_draw(&sprites, &c, ACTION_REST, 1234, 0, 0, 1112, 688);
    uint64_t h = hash_canvas(&c);
#ifndef _WIN32
    /* Pinned after Nicolas approved the look on 2026-11-07; a change must be
     * looked at by eye. Linux only: the poses use sinf, whose last bits may
     * differ on another C library. Re-pinned the same day: resting animals
     * now breathe, swish and look about, each at its own point of a 6 s
     * cycle, and their eyes blink; looked at by eye. Re-pinned 2026-11-11:
     * the wolf, goat, otter, elephant, armadillo and fox drawn from their PixelLab frames (art.h);
     * looked at by eye in the lab, Nicolas's approval of the new look still
     * to come. */
    static const uint64_t EXPECTED = 0xed177585796c31e9u;
    if (h != EXPECTED) {
        printf("lab: 0x%llxu\n", (unsigned long long)h);
    }
    CHECK(h == EXPECTED);
#else
    (void)h;
#endif
    canvas_free(&c);
}

int main(void)
{
    CHECK(sprite_cache_init(&sprites, SPRITE_ENTRIES, SPRITE_DETAIL_ENTRIES));
    test_lab_shows_every_species_once();
    test_lab_stays_in_its_area();
    test_lab_cells_not_empty();
    test_every_action_draws();
    test_hunters_attack_a_deer();
    test_lab_particles();
    test_lab_scale_at_window_size();
    test_lab_tiny_area_is_safe();
    test_lab_pixels();
    sprite_cache_free(&sprites);
    TEST_SUMMARY();
}
