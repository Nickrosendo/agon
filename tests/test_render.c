#include "test.h"

#include "palette.h"
#include "world.h" /* Plant */

static int same(Colour a, Colour b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void test_ground_follows_its_water(void)
{
    Colour desert = palette_ground(0.5f, 0.0f);
    Colour plain = palette_ground(0.5f, 0.6f);
    Colour soaked = palette_ground(0.5f, 1.0f); /* the top must stay in the table */
    CHECK(same(plain, palette_soil(0.5f)));
    CHECK(!same(desert, plain));
    CHECK(!same(soaked, plain));
    /* Lighter when dry, darker when soaked. */
    CHECK(desert.r > plain.r);
    CHECK(soaked.r < plain.r);
}

static void test_display_hue_skips_the_greens(void)
{
    /* Every inherited hue, in steps of 0.001, lands outside the plants'
     * greens, in 0 .. 1. */
    int inside = 0, outside_wheel = 0;
    for (int i = 0; i < 1000; i++) {
        float shown = render_display_hue((float)i / 1000.0f);
        if (shown >= RENDER_PLANT_HUE_FROM && shown < RENDER_PLANT_HUE_TO) {
            inside++;
        }
        if (shown < 0.0f || shown >= 1.0f) {
            outside_wheel++;
        }
    }
    CHECK(inside == 0);
    CHECK(outside_wheel == 0);
}

static void test_display_hue_keeps_the_order(void)
{
    /* Close hues stay close and in the same order, so related families
     * still look related: the wheel is squeezed, not shuffled. */
    float before = render_display_hue(0.0f);
    CHECK(before == 0.0f); /* red stays red */
    for (int i = 1; i < 1000; i++) {
        float shown = render_display_hue((float)i / 1000.0f);
        CHECK(shown > before);
        before = shown;
    }
}

static void test_plant_hues_stay_in_the_greens(void)
{
    /* Every water need gives a hue inside the plants' greens, which creatures
     * never use, the driest and the thirstiest differ, and a need of exactly 1
     * does not reach past the last step. */
    int outside = 0;
    for (int b = 0; b <= 100; b++) {
        float hue = render_plant_hue((float)b / 100.0f);
        if (hue < RENDER_PLANT_HUE_FROM || hue > RENDER_PLANT_HUE_TO) {
            outside++;
        }
    }
    CHECK(outside == 0);
    CHECK(render_plant_hue(0.0f) != render_plant_hue(1.0f));
    /* Close needs share a hue: render_world skips cells that have not changed. */
    CHECK(render_plant_hue(0.5f) == render_plant_hue(0.51f));
}

/* Ripe fruit shows, what a plant offers shows, and huge fruit from a damaged
 * save stays safe. */
static void test_plant_colour_shows_fruit(void)
{
    Plant plain = {.water_need = 0.5f};
    Plant ripe = plain;
    ripe.fruit = 2.0f;
    Colour a = palette_plant(&plain), b = palette_plant(&ripe);
    CHECK(a.r != b.r || a.g != b.g || a.b != b.b);
    Plant huge = plain;
    huge.fruit = 1e30f;
    Plant full = plain;
    full.fruit = 3.0f;
    Colour c = palette_plant(&huge), d = palette_plant(&full);
    CHECK(c.r == d.r && c.g == d.g && c.b == d.b);
    Plant tree = plain;
    tree.fruit_share = 1.0f;
    Colour e = palette_plant(&tree);
    CHECK(a.r != e.r || a.g != e.g || a.b != e.b);
}

int main(void)
{
    test_ground_follows_its_water();
    test_plant_hues_stay_in_the_greens();
    test_plant_colour_shows_fruit();
    test_display_hue_skips_the_greens();
    test_display_hue_keeps_the_order();
    TEST_SUMMARY();
}
