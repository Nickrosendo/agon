#include "test.h"

#include <math.h>   /* fabsf */
#include <stdlib.h> /* abs */

#include "camera.h"

static void test_ppc_levels(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    c.level = CAMERA_CLOSE;
    CHECK(camera_ppc(&c) == 32);
    c.level = CAMERA_NORMAL;
    CHECK(camera_ppc(&c) == 16);
    c.level = CAMERA_FAR;
    CHECK(camera_ppc(&c) == 8);
    c.level = CAMERA_WORLD;
    CHECK(camera_ppc(&c) == 4);
    Camera big;
    camera_init(&big, 4000, 2000, 1120, 720);
    big.level = CAMERA_WORLD;
    CHECK(camera_ppc(&big) == 1);
    Camera two;
    camera_init(&two, 256, 144, 600, 300);
    two.level = CAMERA_WORLD;
    CHECK(camera_ppc(&two) == 2);
}

static void test_sprite_sizes(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    const int sizes[CAMERA_LEVELS] = {16, 16, 8, 0};
    const int scales[CAMERA_LEVELS] = {2, 1, 1, 1};
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        c.level = level;
        CHECK(camera_sprite_size(&c) == sizes[level]);
        CHECK(camera_sprite_scale(&c) == scales[level]);
    }
}

/* Screen to world and back lands within one pixel, including with the
 * world's edge in the middle of the view. */
static void test_round_trip_every_level(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    const float centres[] = {128.0f, 0.5f, 255.5f};
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        c.level = level;
        for (int k = 0; k < 3; k++) {
            camera_centre_on(&c, centres[k], 72.0f);
            for (int sx = 0; sx < 1120; sx += 37) {
                for (int sy = 0; sy < 720; sy += 41) {
                    float wx, wy;
                    int bx, by;
                    camera_to_world(&c, sx, sy, &wx, &wy);
                    CHECK(wx >= 0.0f && wx < 256.0f);
                    CHECK(wy >= 0.0f && wy < 144.0f);
                    if (level == CAMERA_WORLD &&
                        (sx < 48 || sx >= 1072 || sy < 72 || sy >= 648)) {
                        continue; /* the black margin: not world at all */
                    }
                    camera_to_screen(&c, wx, wy, &bx, &by);
                    CHECK(abs(bx - sx) <= 1);
                    CHECK(abs(by - sy) <= 1);
                }
            }
        }
    }
}

static void test_zoom_keeps_mouse_point(void)
{
    /* Close, Normal and Far all wrap here; World fits the whole world, so
     * it is centred on black and the point cannot stay. */
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    c.level = CAMERA_FAR;
    camera_centre_on(&c, 30.0f, 20.0f);
    const int mx = 300, my = 200;
    for (int step = 0; step < 2; step++) {
        float wx, wy, after_x, after_y;
        camera_to_world(&c, mx, my, &wx, &wy);
        camera_zoom(&c, 1, mx, my);
        camera_to_world(&c, mx, my, &after_x, &after_y);
        CHECK(fabsf(after_x - wx) < 1.0f);
        CHECK(fabsf(after_y - wy) < 1.0f);
    }
    CHECK(c.level == CAMERA_CLOSE);
}

static void test_zoom_stops_at_ends(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    camera_zoom(&c, 50, 10, 10);
    CHECK(c.level == CAMERA_CLOSE);
    camera_zoom(&c, -50, 10, 10);
    CHECK(c.level == CAMERA_WORLD);
    camera_zoom(&c, -1, 10, 10);
    CHECK(c.level == CAMERA_WORLD);
}

static void test_wrap_picks_nearest_image(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    camera_centre_on(&c, 1.0f, 72.0f);
    int sx, sy;
    camera_to_screen(&c, 255.0f, 72.0f, &sx, &sy);
    /* Two cells left of the middle, not 254 cells right of it. */
    CHECK(sx == 560 - 2 * 16);
    CHECK(sy == 360);
}

static void test_centre_on_across_edge(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    camera_centre_on(&c, 255.9f, 72.0f);
    int before, after, sy;
    camera_to_screen(&c, 5.0f, 72.0f, &before, &sy);
    camera_centre_on(&c, 0.1f, 72.0f);
    camera_to_screen(&c, 5.0f, 72.0f, &after, &sy);
    CHECK(abs(after - before) < camera_ppc(&c));
}

static void test_pan_moves_by_pixels(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    camera_centre_on(&c, 100.0f, 50.0f);
    camera_pan(&c, 32, -16);
    CHECK(fabsf(c.cx - 102.0f) < 0.001f);
    CHECK(fabsf(c.cy - 49.0f) < 0.001f);
    camera_centre_on(&c, 255.0f, 0.5f);
    camera_pan(&c, 32, -16); /* both across the edge */
    CHECK(fabsf(c.cx - 1.0f) < 0.001f);
    CHECK(fabsf(c.cy - 143.5f) < 0.001f);
}

static void test_tiny_world_fits(void)
{
    Camera c;
    camera_init(&c, 4, 3, 1120, 720);
    c.level = CAMERA_WORLD;
    CHECK(camera_world_fits(&c));
    CHECK(camera_ppc(&c) == 4);
    int sx, sy;
    camera_to_screen(&c, 0.0f, 0.0f, &sx, &sy);
    CHECK(sx == (1120 - 16) / 2);
    CHECK(sy == (720 - 12) / 2);
    /* Panning changes nothing while it fits: it is centred. */
    camera_pan(&c, 500, 500);
    int ax, ay;
    camera_to_screen(&c, 0.0f, 0.0f, &ax, &ay);
    CHECK(ax == sx && ay == sy);
    Camera big;
    camera_init(&big, 256, 144, 1120, 720);
    CHECK(!camera_world_fits(&big));
}

static void test_cells_cover_the_view(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    const float xs[] = {128.0f, 0.2f, 255.7f, 13.37f};
    for (int k = 0; k < 4; k++) {
        camera_centre_on(&c, xs[k], xs[k] / 2.0f);
        CameraCells v;
        camera_cells(&c, &v);
        int ppc = camera_ppc(&c);
        CHECK(v.first_x >= 0 && v.first_x < 256);
        CHECK(v.first_y >= 0 && v.first_y < 144);
        CHECK(v.screen_x <= 0 && v.screen_x > -ppc);
        CHECK(v.screen_y <= 0 && v.screen_y > -ppc);
        CHECK(v.screen_x + v.cols * ppc >= 1120);
        CHECK(v.screen_x + (v.cols - 1) * ppc < 1120);
        CHECK(v.screen_y + v.rows * ppc >= 720);
        CHECK(v.screen_y + (v.rows - 1) * ppc < 720);
        /* The grid agrees with camera_to_screen. */
        int sx, sy;
        camera_to_screen(&c, (float)v.first_x, (float)v.first_y, &sx, &sy);
        CHECK(sx == v.screen_x);
        CHECK(sy == v.screen_y);
    }
    /* A fitting world: every cell once, centred. */
    c.level = CAMERA_WORLD;
    CameraCells v;
    camera_cells(&c, &v);
    CHECK(v.first_x == 0 && v.cols == 256);
    CHECK(v.first_y == 0 && v.rows == 144);
    CHECK(v.screen_x == 48 && v.screen_y == 72);
}

static void test_zero_view(void)
{
    Camera c;
    camera_init(&c, 256, 144, 1120, 720);
    camera_resize(&c, 0, 0);
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        c.level = level;
        CHECK(camera_ppc(&c) >= 1);
        int sx, sy;
        float wx, wy;
        camera_to_screen(&c, 10.0f, 10.0f, &sx, &sy);
        camera_to_world(&c, 0, 0, &wx, &wy);
        camera_pan(&c, 5, 5);
        camera_centre_on(&c, 3.0f, 3.0f);
        CameraCells v;
        camera_cells(&c, &v);
        CHECK(v.cols == 0 && v.rows == 0);
        (void)camera_world_fits(&c);
    }
    c.level = CAMERA_NORMAL;
    camera_zoom(&c, 1, 0, 0);
    camera_zoom(&c, -1, 0, 0);
    camera_resize(&c, -3, -3); /* never stored as negative */
    CHECK(c.view_w == 0 && c.view_h == 0);
}

static void test_on_world_says_no_on_the_black(void)
{
    Camera c;
    /* 4 by 3 cells at 32 pixels each, centred in 320 by 180. */
    camera_init(&c, 4, 3, 320, 180);
    c.level = CAMERA_CLOSE;
    CHECK(camera_on_world(&c, 160, 90));
    CHECK(!camera_on_world(&c, 0, 90));
    CHECK(!camera_on_world(&c, 160, 0));
    CHECK(!camera_on_world(&c, 319, 179));
    /* A world bigger than the view fills it, so everywhere is world. */
    camera_init(&c, 256, 144, 320, 180);
    c.level = CAMERA_CLOSE;
    CHECK(camera_on_world(&c, 0, 0));
    CHECK(camera_on_world(&c, 319, 179));
}

int main(void)
{
    test_ppc_levels();
    test_sprite_sizes();
    test_round_trip_every_level();
    test_zoom_keeps_mouse_point();
    test_zoom_stops_at_ends();
    test_wrap_picks_nearest_image();
    test_centre_on_across_edge();
    test_pan_moves_by_pixels();
    test_tiny_world_fits();
    test_cells_cover_the_view();
    test_zero_view();
    test_on_world_says_no_on_the_black();
    TEST_SUMMARY();
}
