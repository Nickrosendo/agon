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

/* Draws a hunter beside its prey at Normal zoom, biting or not, and says how
 * many pixels are the fight's red. */
static int red_pixels(int action)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 16, 16));
    for (int i = 0; i < s.pool.capacity; i++) {
        if (s.pool.items[i].alive) {
            pool_kill(&s.pool, &s.world, i, &s.set);
        }
    }
    for (int i = 0; i < 16 * 16; i++) {
        s.world.terrain[i] = TERRAIN_SOIL;
        s.world.veg[i] = 0;
        s.world.carrion[i] = 0.0f;
    }
    int hunter = pool_spawn(&s.pool, &s.world, 8, 8);
    int prey = pool_spawn(&s.pool, &s.world, 9, 8);
    s.pool.items[hunter].current.kind = (uint8_t)action;
    s.pool.items[hunter].genome.g[GENE_MEAT] = 1.0f;
    s.pool.items[prey].genome.g[GENE_PLANT] = 1.0f;
    s.pool.items[prey].energy = 10.0f;
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 16, 16, VIEW_W, VIEW_H);
    scene_update(&sc, &s, 10);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 110, 0, 0);
    int red = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        red += c.px[i] == canvas_rgb(255, 70, 50);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
    return red;
}

static void test_fight_draws_a_mark(void)
{
    CHECK(red_pixels(ACT_REST) == 0);
    CHECK(red_pixels(ACT_BITE) > 0);
}

int main(void)
{
    test_drawing_changes_nothing();
    test_empty_view_draws_nothing();
    test_tiny_world_on_black();
    test_fight_draws_a_mark();
    TEST_SUMMARY();
}
