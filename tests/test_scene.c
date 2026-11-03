#include "test.h"

#include <math.h>  /* fabsf */
#include <stdio.h> /* snprintf */

#include "brain_script.h"
#include "family.h"
#include "inspect.h"
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
    Families f;
    CHECK(family_init(&f, drawn.pool.capacity));
    for (int t = 0; t < 2000; t++) {
        sim_step(&plain);
        sim_step(&drawn);
        scene_update(&sc, &drawn, 10);
        cam.level = t % CAMERA_LEVELS;
        scene_draw_world(&sc, &c, &cam, &drawn, 0.5f, (uint64_t)t * 33u,
                         (uint64_t)t * 33u, 0, 0);
        /* The panel's bodies, and asking which creature was clicked on. */
        family_update(&f, &drawn);
        scene_draw_strip(&c, &f, family_largest(&f, &drawn), 0, 0, VIEW_W);
        int slot = t % drawn.pool.capacity;
        if (drawn.pool.items[slot].alive) {
            scene_draw_followed(&sc, &c, &drawn, &drawn.pool.items[slot],
                                (uint64_t)t * 33u, 0, 0);
        }
    }
    CHECK(sim_checksum(&plain) == sim_checksum(&drawn));
    family_free(&f);
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

/* A world of bare soil with nobody in it, for placing creatures by hand. */
static void make_empty_sim(Sim *s, int w, int h)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "script");
    set.start_share = 0.0f;
    set.min_creatures = 0;
    set.veg_start_chance = 0.0f;
    CHECK(sim_init(s, &set, 7, w, h));
    for (int i = 0; i < w * h; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
    }
}

static void test_click_picks_within_two_cells(void)
{
    Sim s;
    make_empty_sim(&s, 32, 32);
    int near = pool_spawn(&s.pool, &s.world, 10, 10);
    int far = pool_spawn(&s.pool, &s.world, 13, 10);
    CHECK(near >= 0 && far >= 0);
    Inspect in = {0};

    /* On it. */
    scene_click(&in, &s, 10.5f, 10.5f);
    CHECK(in.on && inspect_creature(&in, &s) == &s.pool.items[near]);
    /* Two cells away, still it: the nearest wins over the one further off. */
    scene_click(&in, &s, 12.5f, 10.5f);
    CHECK(inspect_creature(&in, &s) == &s.pool.items[far]);
    scene_click(&in, &s, 8.5f, 10.5f);
    CHECK(inspect_creature(&in, &s) == &s.pool.items[near]);
    /* Three cells away from anyone: bare ground lets go. */
    scene_click(&in, &s, 7.5f, 10.5f);
    CHECK(!in.on && !in.following);
    sim_free(&s);
}

static void test_click_finds_across_the_wrapped_edge(void)
{
    Sim s;
    make_empty_sim(&s, 32, 32);
    int slot = pool_spawn(&s.pool, &s.world, 0, 0);
    CHECK(slot >= 0);
    Inspect in = {0};
    scene_click(&in, &s, 31.5f, 31.5f);
    CHECK(inspect_creature(&in, &s) == &s.pool.items[slot]);
    sim_free(&s);
}

static void test_follow_centres_on_the_creature(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    int slot = pool_spawn(&s.pool, &s.world, 20, 30);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10);
    Camera cam;
    camera_init(&cam, 64, 64, 64, 64);
    cam.level = CAMERA_CLOSE;
    Inspect in = {0};
    scene_click(&in, &s, 20.5f, 30.5f);
    CHECK(scene_follow(&sc, &cam, &in, &s, 0.0f) == &s.pool.items[slot]);
    CHECK(fabsf(cam.cx - 20.0f) < 0.01f && fabsf(cam.cy - 30.0f) < 0.01f);
    scene_free(&sc);
    sim_free(&s);
}

static void test_followed_creature_dies_camera_stays(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    int slot = pool_spawn(&s.pool, &s.world, 20, 30);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10);
    Camera cam;
    camera_init(&cam, 64, 64, 64, 64);
    cam.level = CAMERA_CLOSE;
    Inspect in = {0};
    scene_click(&in, &s, 20.5f, 30.5f);
    CHECK(scene_follow(&sc, &cam, &in, &s, 0.0f) != NULL);

    /* It dies, and a newborn takes its slot somewhere else: a different id, so
     * the camera must not fly off after the newborn. */
    pool_kill(&s.pool, &s.world, slot, &s.set);
    int newborn = pool_spawn(&s.pool, &s.world, 50, 5);
    CHECK(newborn == slot);
    s.tick++;
    scene_update(&sc, &s, 10);
    CHECK(scene_follow(&sc, &cam, &in, &s, 0.0f) == NULL);
    CHECK(!in.following);
    CHECK(fabsf(cam.cx - 20.0f) < 0.01f && fabsf(cam.cy - 30.0f) < 0.01f);
    scene_free(&sc);
    sim_free(&s);
}

/* r: ids restart from 0, so the same id can come up on a stranger. */
static void test_new_world_lets_go(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    CHECK(pool_spawn(&s.pool, &s.world, 20, 30) >= 0);
    Inspect in = {0};
    scene_click(&in, &s, 20.5f, 30.5f);
    CHECK(in.following);
    CHECK(sim_reseed(&s, 9));
    for (int i = 0; i < 64 * 64; i++) {
        s.world.terrain[i] = TERRAIN_SOIL;
    }
    int stranger = pool_spawn(&s.pool, &s.world, 40, 40);
    CHECK(stranger >= 0);
    /* The danger itself: without letting go, the old id finds a stranger. */
    CHECK(inspect_creature(&in, &s) == &s.pool.items[stranger]);
    inspect_stop(&in);
    CHECK(inspect_creature(&in, &s) == NULL);
    CHECK(!in.on);
    sim_free(&s);
}

/* A glide over the world's edge: the camera moves one cell the short way. */
static void test_follow_across_the_edge_takes_the_short_way(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    int slot = pool_spawn(&s.pool, &s.world, 63, 10);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10);
    Camera cam;
    camera_init(&cam, 64, 64, 64, 64);
    cam.level = CAMERA_CLOSE;
    Inspect in = {0};
    scene_click(&in, &s, 63.5f, 10.5f);
    CHECK(scene_follow(&sc, &cam, &in, &s, 0.0f) != NULL);
    /* It steps to x = 0, the other side of the array. */
    s.world.occupant[world_idx(&s.world, 63, 10)] = -1;
    s.world.occupant[world_idx(&s.world, 0, 10)] = slot;
    s.pool.items[slot].x = 0;
    s.tick++;
    scene_update(&sc, &s, 10);
    float before = cam.cx;
    for (int f = 0; f <= 10; f++) {
        CHECK(scene_follow(&sc, &cam, &in, &s, (float)f / 10.0f) != NULL);
        float moved = fabsf(cam.cx - before);
        /* Wrapped: 63.9 to 0.1 is a small step, not 63.8 cells. */
        if (moved > 32.0f) {
            moved = 64.0f - moved;
        }
        CHECK(moved < 0.5f);
        before = cam.cx;
    }
    scene_free(&sc);
    sim_free(&s);
}

static void test_layout_gives_the_panel_way_first(void)
{
    Layout l;
    scene_layout(&l, 1280, 720);
    CHECK(!l.too_small && l.has_panel);
    CHECK(l.view_x == 0 && l.view_y == SCENE_TEXT_H);
    CHECK(l.view_w + SCENE_PANEL_W == 1280);
    CHECK(l.panel_x == l.view_w);
    /* Title and help lines take one row each. */
    CHECK(l.view_h == 720 - 2 * SCENE_TEXT_H);
    CHECK(l.help_y == 720 - SCENE_TEXT_H);

    /* Too narrow for both: the world takes the width. */
    scene_layout(&l, SCENE_MIN_W + SCENE_PANEL_W - 1, 720);
    CHECK(!l.too_small && !l.has_panel);
    CHECK(l.view_w == SCENE_MIN_W + SCENE_PANEL_W - 1);

    scene_layout(&l, SCENE_MIN_W - 1, 720);
    CHECK(l.too_small);
    CHECK(l.view_w == 0 && l.view_h == 0);
    scene_layout(&l, 1280, SCENE_MIN_H - 1);
    CHECK(l.too_small);
    /* Minimised: nothing at all. */
    scene_layout(&l, 0, 0);
    CHECK(l.too_small && l.view_w == 0 && l.view_h == 0);
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

/*
 * The look, pinned, like SEED_7_AFTER_500 in test_sim.c. If this fails, the
 * picture changed: look at it first, and only then update the numbers, saying
 * why in the commit. Symbols are computed with sinf, so another system's maths
 * library may legitimately give other numbers.
 */
static void test_known_world_pixels(void)
{
    static const uint64_t EXPECTED[CAMERA_LEVELS] = {
        0x2ff27f9f210e01a5u, 0x996cbe6d86deb305u, 0x384e0821aaa96c9fu, 0x23c728eeb28b20a5u,
    };
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Scene sc;
    CHECK(scene_init(&sc, &s));
    for (int t = 0; t < 300; t++) {
        sim_step(&s);
        scene_update(&sc, &s, 10);
    }
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 32, VIEW_W, VIEW_H);
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        cam.level = level;
        canvas_fill(&c, canvas_rgb(255, 0, 255));
        scene_draw_world(&sc, &c, &cam, &s, 0.5f, 1000, 1000, 0, 0);
        uint64_t h = hash_canvas(&c);
        if (h != EXPECTED[level]) {
            printf("level %d: 0x%llxu\n", level, (unsigned long long)h);
        }
        CHECK(h == EXPECTED[level]);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

/* The ground is kept between frames: it must come back when, and only when,
 * something it shows has changed. */
static void test_ground_layer_refreshes_when_it_should(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10);
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    uint64_t first = hash_canvas(&c);

    /* Same tick, same view: not drawn again, so a change does not show yet. In
     * the real program the world cannot change without a tick. */
    for (int i = 0; i < 64 * 64; i++) {
        s.world.terrain[i] = TERRAIN_WATER;
    }
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    CHECK(hash_canvas(&c) == first);

    /* A tick shows it. */
    s.tick++;
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    uint64_t water0 = hash_canvas(&c);
    CHECK(water0 != first);
    /* The next picture of the water. */
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 250, 0, 0, 0);
    CHECK(hash_canvas(&c) != water0);
    /* Moving or zooming the camera. */
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    CHECK(hash_canvas(&c) == water0);
    cam.level = CAMERA_FAR;
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    CHECK(hash_canvas(&c) != water0);
    cam.level = CAMERA_NORMAL;
    /* A smaller view, then the frame is whatever size it is. */
    camera_resize(&cam, VIEW_W / 2, VIEW_H / 2);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    CHECK(sc.ground.w == VIEW_W / 2);
    /* r: the same tick number on a new world. */
    camera_resize(&cam, VIEW_W, VIEW_H);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    for (int i = 0; i < 64 * 64; i++) {
        s.world.terrain[i] = TERRAIN_ROCK;
    }
    scene_reset(&sc);
    scene_update(&sc, &s, 10);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    CHECK(hash_canvas(&c) != water0);
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

static void test_strip_and_enlarged_bodies(void)
{
    Sim s;
    make_empty_sim(&s, 32, 32);
    int slot = pool_spawn(&s.pool, &s.world, 5, 5);
    s.pool.items[slot].genome.g[GENE_SIZE] = 1.0f;
    s.pool.items[slot].genome.g[GENE_HUE] = 0.3f;
    s.pool.items[slot].lineage = 5;
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    family_update(&f, &s);
    s.tick = f.next_tick;
    family_update(&f, &s);
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    uint32_t magenta = canvas_rgb(255, 0, 255);

    /* Two samples: two bodies, each a tile wide, side by side. */
    canvas_fill(&c, magenta);
    scene_draw_strip(&c, &f, 5, 0, 0, 16 * FAMILY_STRIP);
    int first = 0, second = 0, third = 0;
    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 16; i++) {
            first += c.px[j * VIEW_W + i] != magenta;
            second += c.px[j * VIEW_W + 16 + i] != magenta;
            third += c.px[j * VIEW_W + 32 + i] != magenta;
        }
    }
    CHECK(first > 0 && second > 0 && third == 0);
    /* A strip narrower than its bodies shows those that fit. */
    canvas_fill(&c, magenta);
    scene_draw_strip(&c, &f, 5, 0, 0, 16);
    CHECK(c.px[8 * VIEW_W + 16 + 8] == magenta);
    /* No record of this family: nothing drawn, no crash. */
    canvas_fill(&c, magenta);
    scene_draw_strip(&c, &f, 77, 0, 0, VIEW_W);
    scene_draw_strip(&c, &f, FAMILY_NONE, 0, 0, VIEW_W);
    CHECK(c.px[8 * VIEW_W + 8] == magenta);

    /* Enlarged: four times the size of the 16 pixel body. */
    canvas_fill(&c, magenta);
    scene_draw_followed(&sc, &c, &s, &s.pool.items[slot], 0, 10, 10);
    int bx0 = VIEW_W, bx1 = -1;
    for (int j = 0; j < VIEW_H; j++) {
        for (int i = 0; i < VIEW_W; i++) {
            if (c.px[j * VIEW_W + i] != magenta) {
                bx0 = i < bx0 ? i : bx0;
                bx1 = i > bx1 ? i : bx1;
            }
        }
    }
    CHECK(bx0 == 10 && bx1 == 10 + SCENE_BODY_BIG - 1);
    /* Enlarged, from a family's newest sample. */
    FamilySample out[FAMILY_STRIP];
    int n = family_strip(&f, 5, out);
    CHECK(n == 2);
    canvas_fill(&c, magenta);
    scene_draw_sample(&c, &out[n - 1], 0, 0, 0);
    CHECK(c.px[0] != magenta); /* its background */
    canvas_free(&c);
    family_free(&f);
    scene_free(&sc);
    sim_free(&s);
}

/* The panel's bottom part: the strip goes first when there is not room. */
static void test_layout_family_area(void)
{
    Layout l;
    scene_layout(&l, 1280, 720);
    CHECK(l.show_enlarged && l.show_strip);
    CHECK(l.panel_rows == (720 - 2 * SCENE_TEXT_H) / FONT_H - l.family_rows);
    CHECK(l.family_y == l.panel_y + l.panel_rows * FONT_H);

    /* Lower and lower: the strip goes, then the enlarged view. */
    bool lost_strip = false, lost_enlarged = false;
    for (int h = 720; h >= SCENE_MIN_H; h -= FONT_H) {
        scene_layout(&l, 1280, h);
        CHECK(l.panel_rows >= 0);
        CHECK(l.panel_y + (l.panel_rows + l.family_rows) * FONT_H <= h - SCENE_TEXT_H);
        if (!l.show_strip) {
            lost_strip = true;
        }
        if (!l.show_enlarged) {
            lost_enlarged = true;
            CHECK(lost_strip);
            CHECK(!l.show_strip);
        }
    }
    CHECK(lost_strip && lost_enlarged);
}

int main(void)
{
    /* Tests register this one themselves: the real program never has it. */
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_drawing_changes_nothing();
    test_empty_view_draws_nothing();
    test_tiny_world_on_black();
    test_fight_draws_a_mark();
    test_click_picks_within_two_cells();
    test_click_finds_across_the_wrapped_edge();
    test_follow_centres_on_the_creature();
    test_followed_creature_dies_camera_stays();
    test_new_world_lets_go();
    test_follow_across_the_edge_takes_the_short_way();
    test_layout_gives_the_panel_way_first();
    test_strip_and_enlarged_bodies();
    test_known_world_pixels();
    test_ground_layer_refreshes_when_it_should();
    test_layout_family_area();
    TEST_SUMMARY();
}
