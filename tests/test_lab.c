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

static void check_cells_not_empty(int w, int h)
{
    Canvas c;
    CHECK(canvas_init(&c, w, h));
    lab_draw(&sprites, &c, ACTION_REST, 0, 0, 0, w, h);
    uint32_t bg = canvas_rgb(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
                             PALETTE_TEXT_BACKGROUND.b);
    /* The cells sit in three equal columns from after the name column; find each
     * strip by its sky pixels, and count animal pixels between its edges. */
    int cells_x = 8 + 96 + 8, cell_w = (w - 8 - cells_x - 2 * 8) / LAB_COLUMNS;
    int row_h = (h - 4 - (16 + 4)) / ROLE_COUNT;
    for (int role = 0; role < ROLE_COUNT; role++) {
        for (int col = 0; col < LAB_COLUMNS; col++) {
            if (lab_species_at(role, col) < 0) {
                continue;
            }
            int x0 = cells_x + col * (cell_w + 8), y0 = 20 + role * row_h;
            int animal = 0;
            for (int y = y0; y < y0 + row_h - 22; y++) {
                for (int x = x0; x < x0 + cell_w; x++) {
                    uint32_t p = c.px[y * c.w + x];
                    if (!is_scenery(p) && p != bg) {
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
    check_cells_not_empty(960, 688);   /* scale 1, the window's size */
    check_cells_not_empty(1400, 1000); /* scale 2 */
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

int main(void)
{
    CHECK(sprite_cache_init(&sprites, SPRITE_ENTRIES, SPRITE_DETAIL_ENTRIES));
    test_lab_shows_every_species_once();
    test_lab_stays_in_its_area();
    test_lab_cells_not_empty();
    test_lab_tiny_area_is_safe();
    sprite_cache_free(&sprites);
    TEST_SUMMARY();
}
