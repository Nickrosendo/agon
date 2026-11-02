#include "test.h"

#include "render.h"

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

int main(void)
{
    test_display_hue_skips_the_greens();
    test_display_hue_keeps_the_order();
    TEST_SUMMARY();
}
