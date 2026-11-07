#include "test.h"

#include <math.h>  /* fabsf */
#include <stdio.h> /* snprintf */
#include <string.h> /* strlen, strstr */

#include "app.h"
#include "brain_script.h"
#include "climate.h"
#include "family.h"
#include "inspect.h"
#include "palette.h"
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
            pool_kill(&s.pool, &s.world, i, &s.set, DEATH_WOUNDS);
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
    s.pool.items[hunter].heading = HEADING_EAST; /* the prey is ahead */
    s.set.family_peace = 2.0f; /* near-identical genomes would be spared */
    s.set.colour_peace = 0.0f;
    s.pool.items[hunter].genome.g[GENE_MEAT] = 1.0f;
    s.pool.items[prey].genome.g[GENE_LEAF] = 1.0f;
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
    CHECK(red_pixels(ACT_BITE) == 0); /* a bite is only ever a meal */
    CHECK(red_pixels(ACT_ATTACK) > 0);
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
    pool_kill(&s.pool, &s.world, slot, &s.set, DEATH_WOUNDS);
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
 * The look, unpinned until Nicolas approves the look (design section 8). It
 * prints the four hashes, so a change shows, but asserts only what is true of
 * any look: every pixel of the view was drawn (none keeps the magenta fill), and
 * the four zoom levels give four different pictures. When it is pinned again,
 * like SEED_7_AFTER_500 in test_sim.c, a failure means the picture changed: look
 * at it first, and only then update the numbers, saying why in the commit.
 */
static void test_known_world_pixels(void)
{
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
    uint32_t magenta = canvas_rgb(255, 0, 255);
    uint64_t hashes[CAMERA_LEVELS];
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        cam.level = level;
        canvas_fill(&c, magenta);
        scene_draw_world(&sc, &c, &cam, &s, 0.5f, 1000, 1000, 0, 0);
        hashes[level] = hash_canvas(&c);
        printf("level %d: 0x%llxu\n", level, (unsigned long long)hashes[level]);
        int left = 0;
        for (int i = 0; i < VIEW_W * VIEW_H; i++) {
            left += c.px[i] == magenta;
        }
        CHECK(left == 0);
    }
    for (int a = 0; a < CAMERA_LEVELS; a++) {
        for (int b = a + 1; b < CAMERA_LEVELS; b++) {
            CHECK(hashes[a] != hashes[b]);
        }
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

/* When only the water's clock moves, the layer redraws only the cells with
 * water or rain: the picture must be the one a whole redraw would give. */
static void test_water_only_redraw_matches_whole(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    for (int t = 0; t < 50; t++) {
        sim_step(&s);
    }
    /* Rain on some cells, so dry land with rain is redrawn too. */
    for (int i = 0; i < 64 * 32; i += 7) {
        s.world.rain[i] = 1.0f;
    }
    Canvas a, b;
    CHECK(canvas_init(&a, VIEW_W, VIEW_H));
    CHECK(canvas_init(&b, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 32, VIEW_W, VIEW_H);
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        cam.level = level;
        Scene partly, whole_scene;
        CHECK(scene_init(&partly, &s));
        CHECK(scene_init(&whole_scene, &s));
        scene_update(&partly, &s, 10);
        scene_update(&whole_scene, &s, 10);
        scene_draw_world(&partly, &a, &cam, &s, 0.5f, 0, 1000, 0, 0);
        scene_draw_world(&partly, &a, &cam, &s, 0.5f, 250, 1000, 0, 0);
        scene_draw_world(&whole_scene, &b, &cam, &s, 0.5f, 250, 1000, 0, 0);
        CHECK(hash_canvas(&a) == hash_canvas(&b));
        scene_free(&partly);
        scene_free(&whole_scene);
    }
    canvas_free(&a);
    canvas_free(&b);
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

/* A dead world with records set by hand, so its text depends on nothing else. */
static void make_dead_world(Sim *s)
{
    make_empty_sim(s, 64, 64);
    s->seed = 0x2a;
    s->tick = 12345;
    /* The year these dates were written for, so the text does not move with
     * the default year. */
    s->set.ticks_per_year = 100;
    s->records = (WorldRecords){
        .peak_population = 1234,
        .oldest_id = 77,
        .oldest_age = 4567,
        .largest_id = 88,
        .largest_size = 1.5f,
        .ended_at = 12345,
    };
}

static bool lines_hold(char lines[][APP_OVER_COLS], int n, const char *piece)
{
    for (int i = 0; i < n; i++) {
        if (strstr(lines[i], piece) != NULL) {
            return true;
        }
    }
    return false;
}

static void test_game_over_text(void)
{
    Sim s;
    make_dead_world(&s);
    char lines[APP_OVER_LINES][APP_OVER_COLS];
    int n = app_game_over_lines(&s, lines);
    CHECK(n == APP_OVER_LINES);
    CHECK(lines_hold(lines, n, "year 123"));  /* 12,345 ticks at 100 a year */
    CHECK(lines_hold(lines, n, "0x2a"));
    CHECK(lines_hold(lines, n, "1,234"));
    CHECK(lines_hold(lines, n, "#77"));
    CHECK(lines_hold(lines, n, "45.6 y"));
    CHECK(lines_hold(lines, n, "#88"));
    CHECK(lines_hold(lines, n, "1.50"));

    /* A world where nobody ever lived has no one to name. */
    s.records = (WorldRecords){0};
    n = app_game_over_lines(&s, lines);
    CHECK(lines_hold(lines, n, "nobody"));
    CHECK(!lines_hold(lines, n, "#"));
    sim_free(&s);
}

/* The biggest numbers a record can hold still fit, whole and ended. */
static void test_game_over_text_longest(void)
{
    Sim s;
    make_dead_world(&s);
    s.seed = UINT64_MAX;
    s.tick = UINT64_MAX;
    s.records.ended_at = UINT64_MAX;
    s.records.peak_population = 2147483647;
    s.records.oldest_id = UINT32_MAX;
    s.records.oldest_age = UINT32_MAX;
    s.records.largest_id = UINT32_MAX;
    s.records.largest_size = 1e9f;
    char lines[APP_OVER_LINES][APP_OVER_COLS];
    int n = app_game_over_lines(&s, lines);
    for (int i = 0; i < n; i++) {
        CHECK(strlen(lines[i]) < APP_OVER_COLS - 1); /* nothing was cut */
    }
    sim_free(&s);
}

/* The screen's picture, pinned as test_known_world_pixels is. */
static void test_scene_game_over(void)
{
    static const uint64_t EXPECTED = 0x5beacfce67e3d665u;
    Sim s;
    make_dead_world(&s);
    Canvas c;
    CHECK(canvas_init(&c, 640, 360));
    canvas_fill(&c, canvas_rgb(255, 0, 255));
    scene_draw_game_over(&c, &s, "n new world | q quit", 10, 20, 620, 330);
    uint64_t h = hash_canvas(&c);
    if (h != EXPECTED) {
        printf("game over: 0x%llxu\n", (unsigned long long)h);
    }
    CHECK(h == EXPECTED);
    /* Outside the area it was given, nothing is touched. */
    CHECK(c.px[0] == canvas_rgb(255, 0, 255));
    CHECK(c.px[640 * 359 + 639] == canvas_rgb(255, 0, 255));

    /* A view too small for it: cut, and never outside. */
    canvas_fill(&c, canvas_rgb(255, 0, 255));
    scene_draw_game_over(&c, &s, "n new world", 10, 20, 40, 30);
    CHECK(c.px[640 * 20 + 9] == canvas_rgb(255, 0, 255));
    CHECK(c.px[640 * 19 + 10] == canvas_rgb(255, 0, 255));
    CHECK(c.px[640 * 20 + 50] == canvas_rgb(255, 0, 255));
    CHECK(c.px[640 * 50 + 10] == canvas_rgb(255, 0, 255));
    canvas_free(&c);
    sim_free(&s);
}

static void test_panel_pick(void)
{
    Layout l;
    scene_layout(&l, 1280, 720);
    CHECK(l.has_panel && l.show_enlarged);
    int x = l.panel_x + 10;
    int row_y = l.panel_y + PANEL_ROW_TOP_HUNTER * FONT_H;
    CHECK(scene_panel_pick(&l, x, row_y) == PANEL_PICK_TOP_HUNTER);
    CHECK(scene_panel_pick(&l, x, row_y + FONT_H - 1) == PANEL_PICK_TOP_HUNTER);
    CHECK(scene_panel_pick(&l, x, row_y - 1) == PANEL_PICK_NONE);
    CHECK(scene_panel_pick(&l, x, l.panel_y + PANEL_ROW_OLDEST * FONT_H) ==
          PANEL_PICK_OLDEST);
    CHECK(scene_panel_pick(&l, x, l.family_y + FONT_H) ==
          PANEL_PICK_LARGEST_FAMILY);
    /* Off the panel's sides and above it. */
    CHECK(scene_panel_pick(&l, l.panel_x - 1, row_y) == PANEL_PICK_NONE);
    CHECK(scene_panel_pick(&l, l.panel_x + SCENE_PANEL_W, row_y) == PANEL_PICK_NONE);
    CHECK(scene_panel_pick(&l, x, 0) == PANEL_PICK_NONE);

    /* A short window: rows that did not fit cannot be clicked. */
    scene_layout(&l, 1280, 12 * FONT_H);
    CHECK(l.has_panel);
    CHECK(l.panel_rows <= PANEL_ROW_OLDEST);
    CHECK(scene_panel_pick(&l, l.panel_x + 10,
                           l.panel_y + PANEL_ROW_OLDEST * FONT_H) ==
          PANEL_PICK_NONE);
    /* A window with no panel at all. */
    scene_layout(&l, SCENE_MIN_W, 720);
    CHECK(!l.has_panel);
    CHECK(scene_panel_pick(&l, 10, 100) == PANEL_PICK_NONE);
}

/* One soil cell's look, at the closest zoom and the furthest. */
static uint64_t soil_picture(Scene *sc, Sim *s, Canvas *c, Camera *cam, int level,
                             float water, float rain)
{
    for (int i = 0; i < 64 * 64; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
        s->world.fertility[i] = 0.5f;
        s->world.veg[i] = 0;
        s->world.carrion[i] = 0.0f;
        s->world.water[i] = water;
        s->world.rain[i] = rain;
    }
    cam->level = level;
    s->tick++; /* the ground layer is redrawn only on a new tick */
    scene_draw_world(sc, c, cam, s, 0.0f, 0, 0, 0, 0);
    return hash_canvas(c);
}

static void test_ground_shows_water_and_rain(void)
{
    Settings set = settings_default();
    set.start_share = 0.0f;
    Sim s;
    CHECK(sim_init(&s, &set, 1, 64, 64));
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    /* At every zoom, so no drawing path forgets one of them. */
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        uint64_t dry = soil_picture(&sc, &s, &c, &cam, level, 0.0f, 0.0f);
        uint64_t wet = soil_picture(&sc, &s, &c, &cam, level, 1.0f, 0.0f);
        uint64_t rainy = soil_picture(&sc, &s, &c, &cam, level, 1.0f, 1.0f);
        CHECK(dry != wet);
        CHECK(rainy != wet);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

/* A world of one terrain, every cell under this much snow, at Normal zoom. */
static void snow_picture(Scene *sc, Sim *s, Canvas *c, Camera *cam, int terrain,
                         float snow)
{
    for (int i = 0; i < 64 * 64; i++) {
        s->world.terrain[i] = (uint8_t)terrain;
        s->world.fertility[i] = 0.5f;
        s->world.veg[i] = 0;
        s->world.carrion[i] = 0.0f;
        s->world.water[i] = 0.5f;
        s->world.rain[i] = 0.0f;
        s->world.snow[i] = snow;
    }
    cam->level = CAMERA_NORMAL;
    s->tick++; /* the ground layer is redrawn only on a new tick */
    scene_draw_world(sc, c, cam, s, 0.0f, 0, 0, 0, 0);
}

/* How many pixels have every channel at 200 or more, and the mean brightness. */
static int white_pixels(const Canvas *c, float *mean)
{
    int n = 0;
    double sum = 0.0;
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        uint32_t r = (p >> 16) & 0xFFu, g = (p >> 8) & 0xFFu, b = p & 0xFFu;
        n += r >= 200u && g >= 200u && b >= 200u;
        sum += (double)(r + g + b) / 3.0;
    }
    *mean = (float)(sum / (double)(c->w * c->h));
    return n;
}

static void test_snow_whitens_ground(void)
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
    float mean_bare, mean_half, mean_full;
    /* The whole view is cells of the same soil, so the view stands for a cell. */
    snow_picture(&sc, &s, &c, &cam, TERRAIN_SOIL, 0.0f);
    CHECK(white_pixels(&c, &mean_bare) == 0);
    snow_picture(&sc, &s, &c, &cam, TERRAIN_SOIL, 1.0f);
    CHECK(white_pixels(&c, &mean_full) * 10 >= VIEW_W * VIEW_H * 9);
    snow_picture(&sc, &s, &c, &cam, TERRAIN_SOIL, 0.5f);
    white_pixels(&c, &mean_half);
    CHECK(mean_bare < mean_half && mean_half < mean_full);
    /* Snow lies on soil only. */
    const int others[2] = {TERRAIN_WATER, TERRAIN_ROCK};
    for (int k = 0; k < 2; k++) {
        snow_picture(&sc, &s, &c, &cam, others[k], 0.0f);
        uint64_t bare = hash_canvas(&c);
        snow_picture(&sc, &s, &c, &cam, others[k], 1.0f);
        CHECK(hash_canvas(&c) == bare);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

/* One picture of a world of soil, every cell the same but its temperature, with
 * one creature in the middle. Says how many pixels wear the creature's colour. */
static uint64_t region_picture(Scene *sc, Sim *s, Canvas *c, Camera *cam,
                               int level, float temperature, int creature,
                               int *creature_pixels)
{
    for (int i = 0; i < 64 * 64; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
        s->world.fertility[i] = 0.5f;
        s->world.veg[i] = 0;
        s->world.carrion[i] = 0.0f;
        s->world.water[i] = 0.5f;
        s->world.rain[i] = 0.0f;
        s->world.temperature[i] = temperature;
    }
    cam->level = level;
    s->tick++; /* the ground layer is redrawn only on a new tick */
    scene_update(sc, s, 10);
    scene_draw_world(sc, c, cam, s, 0.0f, 0, 0, 0, 0);
    const Creature *cr = &s->pool.items[creature];
    Colour colour = palette_creature(cr->genome.g[GENE_HUE],
                                     render_meat_share(cr->level));
    uint32_t body = canvas_rgb(colour.r, colour.g, colour.b);
    *creature_pixels = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        *creature_pixels += c->px[i] == body;
    }
    return hash_canvas(c);
}

/* The same ground in two regions looks different, at every zoom, and the
 * creature standing on it is drawn exactly the same. */
static void test_region_tint(void)
{
    Settings set = settings_default();
    set.start_share = 0.0f;
    Sim s;
    CHECK(sim_init(&s, &set, 1, 64, 64));
    /* Spawning refuses water and rock, so the cell must be soil first, or the
     * -1 it returns would be used as an index below. */
    s.world.terrain[world_idx(&s.world, 32, 32)] = TERRAIN_SOIL;
    int creature = pool_spawn(&s.pool, &s.world, 32, 32);
    CHECK(creature >= 0);
    if (creature < 0) {
        sim_free(&s);
        return;
    }
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    /* 0.1 is below region_cold_below; 0.45 with water 0.5 is temperate. The
     * checks after each picture make sure the test asks what it means to. */
    for (int level = 0; level < CAMERA_LEVELS; level++) {
        int cold_pixels, mild_pixels;
        uint64_t cold = region_picture(&sc, &s, &c, &cam, level, 0.1f, creature,
                                       &cold_pixels);
        CHECK(climate_region(&s.world, 0, &s.set) == REGION_COLD);
        uint64_t mild = region_picture(&sc, &s, &c, &cam, level, 0.45f, creature,
                                       &mild_pixels);
        CHECK(climate_region(&s.world, 0, &s.set) == REGION_TEMPERATE);
        CHECK(cold != mild);
        CHECK(cold_pixels > 0);
        CHECK(cold_pixels == mild_pixels);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

/* Streaks must move, or rain reads as a stain on the ground. */
static void test_rain_falls_from_frame_to_frame(void)
{
    uint16_t a[16], b[16];
    tiles_build(a, TILE_RAIN, 0, 0, 16, 0);
    tiles_build(b, TILE_RAIN, 0, 0, 16, 1);
    CHECK(memcmp(a, b, sizeof(a)) != 0);
    int pixels = 0;
    for (int j = 0; j < 16; j++) {
        pixels += a[j] != 0;
    }
    CHECK(pixels > 0);
}

/* A creature placed by hand on soil at (x, y), made large by growth. */
static int place_body(Sim *s, int x, int y, float grown)
{
    s->world.terrain[world_idx(&s->world, x, y)] = TERRAIN_SOIL;
    int slot = pool_spawn(&s->pool, &s->world, x, y);
    CHECK(slot >= 0);
    if (slot >= 0) {
        s->pool.items[slot].grown = grown;
        /* Not a newborn, drawn at full size. The drawing side takes the
         * birth tick as tick - age, never below 0, so the clock must be past
         * the age too. */
        s->pool.items[slot].age = 1000;
        s->tick = s->tick > 2000 ? s->tick : 2000;
    }
    return slot;
}

/*
 * Draws the world with nobody in it and again with one body, into a canvas
 * with a border of `border` pixels round the view, and gives the box (in
 * canvas pixels) holding every pixel that changed. False when nothing did.
 */
typedef struct {
    int left, top, right, bottom; /* right and bottom are the last pixels */
} Box;

static bool changed_box(Sim *s, int slot, Camera *cam, int border, Box *out)
{
    Scene sc;
    CHECK(scene_init(&sc, s));
    Canvas a, b;
    CHECK(canvas_init(&a, cam->view_w + 2 * border, cam->view_h + 2 * border));
    CHECK(canvas_init(&b, cam->view_w + 2 * border, cam->view_h + 2 * border));
    uint32_t magenta = canvas_rgb(255, 0, 255);
    canvas_fill(&a, magenta);
    canvas_fill(&b, magenta);
    /* Without: the body is taken away for the first picture, then put back. */
    s->pool.items[slot].alive = false;
    scene_update(&sc, s, 10);
    scene_draw_world(&sc, &a, cam, s, 0.0f, 0, 0, border, border);
    s->pool.items[slot].alive = true;
    scene_update(&sc, s, 10);
    scene_draw_world(&sc, &b, cam, s, 0.0f, 0, 0, border, border);
    *out = (Box){a.w, a.h, -1, -1};
    for (int y = 0; y < a.h; y++) {
        for (int x = 0; x < a.w; x++) {
            if (a.px[y * a.w + x] != b.px[y * a.w + x]) {
                out->left = x < out->left ? x : out->left;
                out->top = y < out->top ? y : out->top;
                out->right = x > out->right ? x : out->right;
                out->bottom = y > out->bottom ? y : out->bottom;
            }
        }
    }
    canvas_free(&a);
    canvas_free(&b);
    scene_free(&sc);
    return out->right >= 0;
}

/* An ordinary body stays in its cell; a grown one covers more, three tiles at
 * most, standing on its own cell. */
static void test_large_body_drawn_large(void)
{
    const float grown[3] = {0.0f, 2.5f, 1000000.0f};
    const int tiles[3] = {1, 2, 3};
    for (int k = 0; k < 3; k++) {
        Sim s;
        make_empty_sim(&s, 64, 64);
        int slot = place_body(&s, 32, 32, grown[k]);
        CHECK(sprite_tiles_for(creature_size(&s.pool.items[slot], &s.set)) ==
              tiles[k]);
        Camera cam;
        camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
        camera_centre_on(&cam, 32.5f, 32.5f);
        int sx, sy;
        camera_to_screen(&cam, 32.0f, 32.0f, &sx, &sy);
        int ppc = camera_ppc(&cam);
        Box box;
        CHECK(changed_box(&s, slot, &cam, 0, &box));
        int extent = tiles[k] * ppc;
        /* Wider than a cell when large, never wider than its tiles plus the
         * outline's pixel each side. */
        CHECK(box.right - box.left + 1 <= extent + 2);
        CHECK(box.bottom - box.top + 1 <= extent + 2);
        if (tiles[k] > 1) {
            CHECK(box.right - box.left + 1 > ppc + 2);
        }
        /* Its feet are on its own cell: nothing below that cell's bottom but
         * the outline. */
        CHECK(box.bottom <= sy + ppc);
        sim_free(&s);
    }
}

/* A giant at the world's corners, in a world that fits the view and in one
 * that does not, at every zoom: nothing is drawn outside the view. Where the
 * whole world shows, nothing is drawn on the black round it either. */
static void test_giant_over_map_edge(void)
{
    const int sizes[2][2] = {{16, 8}, {64, 64}};
    const int corners[4][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};
    int border = 40;
    for (int w = 0; w < 2; w++) {
        int ww = sizes[w][0], wh = sizes[w][1];
        for (int k = 0; k < 4; k++) {
            for (int level = 0; level < CAMERA_LEVELS; level++) {
                Sim s;
                make_empty_sim(&s, ww, wh);
                int x = corners[k][0] * (ww - 1), y = corners[k][1] * (wh - 1);
                int slot = place_body(&s, x, y, 1000000.0f);
                Camera cam;
                camera_init(&cam, ww, wh, VIEW_W, VIEW_H);
                cam.level = level;
                /* In the large world, the giant at a corner of the view. */
                if (w == 1) {
                    camera_centre_on(&cam, (float)x + 9.0f, (float)y + 5.0f);
                }
                Box box;
                if (changed_box(&s, slot, &cam, border, &box)) {
                    CHECK(box.left >= border && box.top >= border);
                    CHECK(box.right < border + VIEW_W);
                    CHECK(box.bottom < border + VIEW_H);
                    if (camera_world_fits(&cam)) {
                        CameraCells v;
                        camera_cells(&cam, &v);
                        int ppc = camera_ppc(&cam);
                        CHECK(box.left >= border + v.screen_x);
                        CHECK(box.top >= border + v.screen_y);
                        CHECK(box.right < border + v.screen_x + ww * ppc);
                        CHECK(box.bottom < border + v.screen_y + wh * ppc);
                    }
                }
                sim_free(&s);
            }
        }
    }
}

/* How many pixels of the scars' colour a body at Normal zoom shows, with
 * `hits` hits taken, the newest `ago` ticks ago. */
static int scar_pixels(float grown, uint32_t hits, uint32_t ago)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    int slot = place_body(&s, 32, 32, grown);
    s.tick = 5000;
    Creature *cr = &s.pool.items[slot];
    cr->genome.g[GENE_HUE] = 0.6f; /* blue: far from the scars' pink */
    for (uint32_t h = 0; h < hits; h++) {
        cr->recent_hits[h % HITS_KEPT] = (Hit){.by = 99, .tick = 5000 - ago - h};
    }
    cr->hits_taken = hits;
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    camera_centre_on(&cam, 32.5f, 32.5f);
    scene_update(&sc, &s, 10);
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    int n = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        n += c.px[i] == canvas_rgb(255, 170, 160);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
    return n;
}

static void test_scars_follow_recent_hits(void)
{
    for (int k = 0; k < 2; k++) {
        float grown = k == 0 ? 0.0f : 1000000.0f;
        CHECK(scar_pixels(grown, 0, 0) == 0);
        CHECK(scar_pixels(grown, 1, 5) > 0);
        CHECK(scar_pixels(grown, 3, 5) > scar_pixels(grown, 1, 5));
        /* Healed: hits older than SCENE_SCAR_TICKS leave no scar. */
        CHECK(scar_pixels(grown, 3, SCENE_SCAR_TICKS + 1) == 0);
    }
}

/* Large bodies drawn at every zoom, with the cursor on one: the world runs as
 * it would with nothing drawn. */
static void test_drawing_large_leaves_fingerprint_equal(void)
{
    Settings set = settings_default();
    Sim plain, drawn;
    CHECK(sim_init(&plain, &set, 3, 64, 32));
    CHECK(sim_init(&drawn, &set, 3, 64, 32));
    /* The same giants in both. */
    for (int i = 0; i < plain.pool.capacity; i += 3) {
        if (plain.pool.items[i].alive) {
            plain.pool.items[i].grown = (float)(i % 4) * 3.0f;
            drawn.pool.items[i].grown = (float)(i % 4) * 3.0f;
        }
    }
    CHECK(sim_checksum(&plain) == sim_checksum(&drawn));
    Scene sc;
    CHECK(scene_init(&sc, &drawn));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 32, VIEW_W, VIEW_H);
    for (int t = 0; t < 1000; t++) {
        sim_step(&plain);
        sim_step(&drawn);
        scene_update(&sc, &drawn, 10);
        cam.level = t % CAMERA_LEVELS;
        scene_draw_world(&sc, &c, &cam, &drawn, 0.5f, (uint64_t)t * 33u,
                         (uint64_t)t * 33u, 0, 0);
        int slot = t % drawn.pool.capacity;
        if (drawn.pool.items[slot].alive) {
            scene_draw_cursor(&sc, &c, &cam, &drawn, &drawn.pool.items[slot],
                              0.5f, 0, 0);
            scene_draw_followed(&sc, &c, &drawn, &drawn.pool.items[slot],
                                (uint64_t)t * 33u, 0, 0);
        }
    }
    CHECK(sim_checksum(&plain) == sim_checksum(&drawn));
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&plain);
    sim_free(&drawn);
}

/* How many pixels scene_draw_names changes on a magenta canvas. */
static int name_pixels(Sim *s, Camera *cam, const Creature *followed)
{
    Scene sc;
    CHECK(scene_init(&sc, s));
    scene_update(&sc, s, 10);
    Canvas c;
    CHECK(canvas_init(&c, cam->view_w, cam->view_h));
    uint32_t magenta = canvas_rgb(255, 0, 255);
    canvas_fill(&c, magenta);
    scene_draw_names(&sc, &c, cam, s, followed, 0.0f, 0, 0);
    int changed = 0;
    for (int i = 0; i < c.w * c.h; i++) {
        changed += c.px[i] != magenta;
    }
    canvas_free(&c);
    scene_free(&sc);
    return changed;
}

/* A giant's name shows under it at Normal zoom, not at Far; an ordinary body
 * gets one only while followed. */
static void test_names_beside_giants(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    int slot = place_body(&s, 32, 32, 6.0f);
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    camera_centre_on(&cam, 32.5f, 32.5f);
    CHECK(name_pixels(&s, &cam, NULL) == 0); /* not a giant yet */
    s.pool.items[slot].tally.flags |= TALLY_GIANT;
    CHECK(name_pixels(&s, &cam, NULL) > 0);
    cam.level = CAMERA_FAR;
    CHECK(name_pixels(&s, &cam, NULL) == 0);
    cam.level = CAMERA_NORMAL;
    s.pool.items[slot].tally.flags = 0;
    CHECK(name_pixels(&s, &cam, &s.pool.items[slot]) > 0);
    sim_free(&s);
}

int main(void)
{
    /* Tests register this one themselves: the real program never has it. */
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_drawing_changes_nothing();
    test_ground_shows_water_and_rain();
    test_snow_whitens_ground();
    test_rain_falls_from_frame_to_frame();
    test_region_tint();
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
    test_water_only_redraw_matches_whole();
    test_layout_family_area();
    test_game_over_text();
    test_game_over_text_longest();
    test_scene_game_over();
    test_panel_pick();
    test_large_body_drawn_large();
    test_giant_over_map_edge();
    test_scars_follow_recent_hits();
    test_drawing_large_leaves_fingerprint_equal();
    test_names_beside_giants();
    TEST_SUMMARY();
}
