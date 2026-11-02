#include "test.h"

#include "scene.h"
#include "settings.h"
#include "sim.h"

#define VIEW_W 320
#define VIEW_H 180

/* Everything a frame draws, at every zoom level in turn: if any of it wrote
 * to the simulation or used its random numbers, the checksums would part. */
static void test_drawing_changes_nothing(void)
{
    Settings set = settings_default();
    Sim plain, drawn;
    CHECK(sim_init(&plain, &set, 3, 64, 32));
    CHECK(sim_init(&drawn, &set, 3, 64, 32));
    Scene sc;
    CHECK(scene_init(&sc, &drawn));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 32, VIEW_W, VIEW_H);
    for (int t = 0; t < 2000; t++) {
        sim_step(&plain);
        sim_step(&drawn);
        scene_update(&sc, &drawn, 10);
        cam.level = t % CAMERA_LEVELS;
        scene_draw_world(&sc, &c, &cam, &drawn, 0.5f, (uint64_t)t * 33u,
                         (uint64_t)t * 33u, 0, 0);
    }
    CHECK(sim_checksum(&plain) == sim_checksum(&drawn));
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&plain);
    sim_free(&drawn);
}

static void test_empty_view_draws_nothing(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 16, 16));
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, 0, 0));
    Camera cam;
    camera_init(&cam, 16, 16, 0, 0);
    scene_update(&sc, &s, 10);
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        cam.level = level;
        scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    }
    /* A real view on an empty canvas, and the reverse: clipping copes. */
    camera_resize(&cam, 100, 100);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    CHECK(c.w == 0);
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

static void test_tiny_world_on_black(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 4, 3));
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    canvas_fill(&c, canvas_rgb(255, 0, 255));
    Camera cam;
    camera_init(&cam, 4, 3, VIEW_W, VIEW_H);
    cam.level = CAMERA_WORLD;
    scene_update(&sc, &s, 10);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    uint32_t black = canvas_rgb(0, 0, 0);
    CHECK(c.px[0] == black);
    CHECK(c.px[VIEW_W - 1] == black);
    CHECK(c.px[VIEW_W * (VIEW_H - 1)] == black);
    CHECK(c.px[VIEW_W * VIEW_H - 1] == black);
    /* The world itself is in the middle, and is not black. */
    CHECK(c.px[VIEW_W * (VIEW_H / 2) + VIEW_W / 2] != black);
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

int main(void)
{
    test_drawing_changes_nothing();
    test_empty_view_draws_nothing();
    test_tiny_world_on_black();
    TEST_SUMMARY();
}
