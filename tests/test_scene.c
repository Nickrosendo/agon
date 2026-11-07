#include "test.h"

#include <math.h>  /* fabsf */
#include <stdio.h> /* snprintf */
#include <string.h> /* strlen, strstr */

#include "app.h"
#include "brain_script.h"
#include "climate.h"
#include "flora.h"
#include "family.h"
#include "inspect.h"
#include "lab.h"
#include "palette.h"
#include "scene.h"
#include "settings.h"
#include "sim.h"
#include "years.h"

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
    long fights = 0; /* slot-frames with a fight on screen */
    long particles = 0; /* particles alive, frame after frame */
    for (int t = 0; t < 2000; t++) {
        sim_step(&plain);
        sim_step(&drawn);
        scene_update(&sc, &drawn, 10, (uint64_t)t * 33u);
        for (int i = 0; i < sc.anim.capacity; i++) {
            fights += sc.anim.slots[i].fight != FIGHT_NONE;
        }
        cam.level = t % CAMERA_LEVELS;
        scene_draw_world(&sc, &c, &cam, &drawn, 0.5f, (uint64_t)t * 33u,
                         (uint64_t)t * 33u, 0, 0);
        particles += sc.particles.count;
        /* The panel's bodies, and asking which creature was clicked on. */
        family_update(&f, &drawn);
        uint32_t largest = family_largest(&f, &drawn);
        scene_draw_strip(&sc, &c, &f, largest, 0, 0, VIEW_W);
        FamilySample samples[FAMILY_STRIP];
        int n = family_strip(&f, largest, samples);
        if (n > 0) {
            scene_draw_sample(&sc, &c, &samples[n - 1], largest, (uint64_t)t * 33u, 0, 0);
        }
        int slot = t % drawn.pool.capacity;
        if (drawn.pool.items[slot].alive) {
            scene_draw_followed(&sc, &c, &drawn, &drawn.pool.items[slot],
                                (uint64_t)t * 33u, 0, 0);
        }
    }
    CHECK(sim_checksum(&plain) == sim_checksum(&drawn));
    CHECK(fights > 0); /* the attack's drawing was in it */
    CHECK(particles > 0); /* and the particles' */
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
    scene_update(&sc, &s, 10, 0);
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
    scene_update(&sc, &s, 10, 0);
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

/* Whether a canvas pixel is near white: every channel at least 191, as a
 * colour pushed 0.75 of the way to white always is. */
static bool near_white(uint32_t px)
{
    return ((px >> 16) & 0xFFu) >= 191u && ((px >> 8) & 0xFFu) >= 191u &&
           (px & 0xFFu) >= 191u;
}

/* A 16 by 16 world of bare soil with a hunter at (8, 8) facing east, doing
 * `action`, and a plant-eater beside it at (9, 8): written to *hunter and
 * *prey. */
static void make_fight(Sim *s, int action, int *hunter, int *prey)
{
    Settings set = settings_default();
    CHECK(sim_init(s, &set, 3, 16, 16));
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            pool_kill(&s->pool, &s->world, i, &s->set, DEATH_WOUNDS);
        }
    }
    for (int i = 0; i < 16 * 16; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
        s->world.veg[i] = 0;
        s->world.carrion[i] = 0.0f;
    }
    *hunter = pool_spawn(&s->pool, &s->world, 8, 8);
    *prey = pool_spawn(&s->pool, &s->world, 9, 8);
    s->pool.items[*hunter].current.kind = (uint8_t)action;
    s->pool.items[*hunter].heading = HEADING_EAST; /* the prey is ahead */
    s->set.family_peace = 2.0f; /* near-identical genomes would be spared */
    s->set.colour_peace = 0.0f;
    s->pool.items[*hunter].genome.g[GENE_MEAT] = 1.0f;
    s->pool.items[*prey].genome.g[GENE_LEAF] = 1.0f;
    s->pool.items[*prey].energy = 10.0f;
}

/* Draws a hunter beside its prey at Normal zoom, doing `action`, after
 * `steps` frames of 33 ms, and says how many pixels are near white. With
 * attacking set, also checks the hunter is drawn attacking. */
static int near_white_count(int action, int steps)
{
    Sim s;
    int hunter, prey;
    make_fight(&s, action, &hunter, &prey);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 16, 16, VIEW_W, VIEW_H);
    uint64_t ms = 0;
    scene_update(&sc, &s, 10, ms);
    for (int i = 0; i < steps; i++) {
        ms += 33u;
        scene_update(&sc, &s, 10, ms);
    }
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, ms, 0, 0);
    if (action == ACT_ATTACK) {
        CHECK(sc.poses[hunter].action == ACTION_ATTACK);
    }
    int white = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        white += near_white(c.px[i]);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
    return white;
}

/* A fight is drawn as the attack itself, not a mark: no red slash any more,
 * and at the bite (28 frames in: c 0.51) the victim flashes white. Biting or
 * resting beside it, nothing flashes. */
static void test_fight_draws_an_attack(void)
{
    int resting = near_white_count(ACT_REST, 28);
    int stalking = near_white_count(ACT_ATTACK, 5);
    int biting = near_white_count(ACT_ATTACK, 28);
    CHECK(near_white_count(ACT_BITE, 28) <= resting + 4); /* a bite is only ever a meal */
    CHECK(stalking <= resting + 4);
    CHECK(biting > resting + 20); /* the victim's whole body, near white */
    if (biting <= resting + 20) {
        printf("  white pixels: resting %d, stalking %d, biting %d\n", resting,
               stalking, biting);
    }
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
    scene_update(&sc, &s, 10, 0);
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
    scene_update(&sc, &s, 10, 0);
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
    scene_update(&sc, &s, 10, 0);
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
    scene_update(&sc, &s, 10, 0);
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
    scene_update(&sc, &s, 10, 0);
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
 * The look at the four zoom levels. Pinned after Nicolas approved the look on
 * 2026-11-07; a change must be looked at by eye. Like SEED_7_AFTER_500 in
 * test_sim.c, a failure means the picture changed: look at it first, and only
 * then update the numbers, saying why in the commit. It also checks what is true
 * of any look: every pixel of the view was drawn (none keeps the magenta fill),
 * and the four zoom levels give four different pictures.
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
        scene_update(&sc, &s, 10, 0);
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
#ifndef _WIN32
        /* Linux only: the water and grass use sinf (see test_lab.c). Re-pinned
         * on 2026-11-07: Far changed because a creature in deep sleep is now
         * drawn lying down; looked at by eye in the window and the lab.
         * Re-pinned again the same day: Far changed because creatures now
         * walk, run, breathe, graze and bite in cycles (the animation stage's
         * poses); Close and Normal kept their hashes. Looked at by eye.
         * Re-pinned for the attack: Far changed because its fights are now
         * drawn as the attack's first frame (this test's body clock stands at
         * 0) instead of a bite with a red slash over the victim; the attack
         * itself was looked at in the lab, frame by frame. */
        static const uint64_t EXPECTED[CAMERA_LEVELS] = {
            0xbe5f57fb7c7bdabdu, 0x5a015897532bfe08u,
            0x257dfcd68e10d4f5u, 0x8467a0fcd9991445u,
        };
        if (hashes[level] != EXPECTED[level]) {
            printf("level %d: 0x%llxu\n", level,
                   (unsigned long long)hashes[level]);
        }
        CHECK(hashes[level] == EXPECTED[level]);
#endif
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
    scene_update(&sc, &s, 10, 0);
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
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, SCENE_DRIFT_MS, 0, 0, 0);
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
    scene_update(&sc, &s, 10, 0);
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
        scene_update(&partly, &s, 10, 0);
        scene_update(&whole_scene, &s, 10, 0);
        scene_draw_world(&partly, &a, &cam, &s, 0.5f, 0, 1000, 0, 0);
        scene_draw_world(&partly, &a, &cam, &s, 0.5f, SCENE_DRIFT_MS, 1000, 0, 0);
        scene_draw_world(&whole_scene, &b, &cam, &s, 0.5f, SCENE_DRIFT_MS, 1000, 0, 0);
        CHECK(hash_canvas(&a) == hash_canvas(&b));
        scene_free(&partly);
        scene_free(&whole_scene);
    }
    canvas_free(&a);
    canvas_free(&b);
    sim_free(&s);
}

/* Counts pixels in the box (x, y, w, h) that are not `colour`. */
static int count_not(const Canvas *c, int x, int y, int w, int h, uint32_t colour)
{
    int n = 0;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            n += c->px[j * c->w + i] != colour;
        }
    }
    return n;
}

/* A sim with one recorded family, lineage 5, of one creature, sampled twice. */
static int make_one_family(Sim *s, Families *f, float size_gene, float grown)
{
    make_empty_sim(s, 32, 32);
    int slot = pool_spawn(&s->pool, &s->world, 5, 5);
    s->pool.items[slot].genome.g[GENE_SIZE] = size_gene;
    s->pool.items[slot].genome.g[GENE_HUE] = 0.3f;
    s->pool.items[slot].grown = grown;
    s->pool.items[slot].lineage = 5;
    CHECK(family_init(f, s->pool.capacity));
    family_update(f, s);
    s->tick = f->next_tick;
    family_update(f, s);
    return slot;
}

static void test_strip_and_enlarged_bodies(void)
{
    Sim s;
    Families f;
    int slot = make_one_family(&s, &f, 1.0f, 0.0f);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10, 0);
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    uint32_t magenta = canvas_rgb(255, 0, 255);
    const int cw = SCENE_STRIP_CELL_W, ch = SCENE_STRIP_CELL_H;

    /* Two samples: two cells side by side, each a body on its ground. */
    canvas_fill(&c, magenta);
    scene_draw_strip(&sc, &c, &f, 5, 0, 0, SCENE_PANEL_W);
    CHECK(count_not(&c, 0, 0, cw, ch, magenta) == cw * ch);
    CHECK(count_not(&c, cw, 0, cw, ch, magenta) == cw * ch);
    CHECK(count_not(&c, 2 * cw, 0, cw, ch, magenta) == 0);
    CHECK(count_not(&c, 0, ch, cw, ch, magenta) == 0);
    /* A strip one cell wide puts the second body on the next row. */
    canvas_fill(&c, magenta);
    scene_draw_strip(&sc, &c, &f, 5, 0, 0, cw);
    CHECK(count_not(&c, cw, 0, cw, ch, magenta) == 0);
    CHECK(count_not(&c, 0, ch, cw, ch, magenta) == cw * ch);
    /* No record of this family: nothing drawn, no crash. */
    canvas_fill(&c, magenta);
    scene_draw_strip(&sc, &c, &f, 77, 0, 0, VIEW_W);
    scene_draw_strip(&sc, &c, &f, FAMILY_NONE, 0, 0, VIEW_W);
    CHECK(count_not(&c, 0, 0, VIEW_W, VIEW_H, magenta) == 0);

    /* Enlarged: the followed creature on its ground, filling exactly its box. */
    canvas_fill(&c, magenta);
    scene_draw_followed(&sc, &c, &s, &s.pool.items[slot], 0, 10, 10);
    CHECK(count_not(&c, 10, 10, SCENE_ENLARGED_W, SCENE_ENLARGED_H, magenta) ==
          SCENE_ENLARGED_W * SCENE_ENLARGED_H);
    CHECK(count_not(&c, 0, 0, VIEW_W, VIEW_H, magenta) ==
          SCENE_ENLARGED_W * SCENE_ENLARGED_H);
    /* Enlarged, from a family's newest sample: the same box. */
    FamilySample out[FAMILY_STRIP];
    int n = family_strip(&f, 5, out);
    CHECK(n == 2);
    canvas_fill(&c, magenta);
    scene_draw_sample(&sc, &c, &out[n - 1], 5, 0, 0, 0);
    CHECK(count_not(&c, 0, 0, VIEW_W, VIEW_H, magenta) ==
          SCENE_ENLARGED_W * SCENE_ENLARGED_H);
    /* The strip's bodies are this family's species, in its colour: some pixel
     * of the first body is one of the palette's body colours. */
    canvas_fill(&c, magenta);
    scene_draw_strip(&sc, &c, &f, 5, 0, 0, SCENE_PANEL_W);
    BodyTraits r = species_round(&out[0].traits);
    SpritePalette pal;
    sprite_palette(&pal, species_pick(5, species_role(&r), out[0].body_size), out[0].hue);
    int coloured = 0;
    for (int j = 0; j < ch; j++) {
        for (int i = 0; i < cw; i++) {
            for (int k = INK_DEEP; k <= INK_BELLY; k++) {
                coloured += c.px[j * VIEW_W + i] == pal.ink[k];
            }
        }
    }
    CHECK(coloured > 0);
    canvas_free(&c);
    family_free(&f);
    scene_free(&sc);
    sim_free(&s);
}

/* How many pixels of the box (x, y, w, h) have one of the species' body colours
 * at this hue. */
static int species_pixels(const Canvas *c, int species, float hue, int x, int y, int w,
                          int h)
{
    SpritePalette pal;
    sprite_palette(&pal, species, hue);
    int n = 0;
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            for (int k = INK_DEEP; k <= INK_BELLY; k++) {
                n += c->px[j * c->w + i] == pal.ink[k];
            }
        }
    }
    return n;
}

/* An armoured family grown large shows elephants or rhinos in the world; its
 * strip must too, not the armadillo a middling size would pick. One rule for
 * both: sprite_body's. */
static void test_strip_shows_the_family_species(void)
{
    Sim s;
    make_empty_sim(&s, 32, 32);
    int slot = pool_spawn(&s.pool, &s.world, 5, 5);
    Creature *cr = &s.pool.items[slot];
    cr->genome.g[GENE_ARMOUR] = 1.0f;
    cr->genome.g[GENE_MEAT] = 0.0f;
    cr->genome.g[GENE_SWIM] = 0.0f;
    cr->genome.g[GENE_HUE] = 0.3f;
    cr->grown = 6.0f;
    cr->lineage = 5;
    int living = sprite_body(cr, &s.set, SPRITE_BASE_NORMAL).species;
    CHECK(SPECIES[living].role == ROLE_ARMOURED);
    CHECK(living != SPECIES_ARMADILLO); /* too large for one */
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    family_update(&f, &s);
    FamilySample out[FAMILY_STRIP];
    CHECK(family_strip(&f, 5, out) == 1);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    scene_draw_strip(&sc, &c, &f, 5, 0, 0, SCENE_PANEL_W);
    int cw = SCENE_STRIP_CELL_W, ch = SCENE_STRIP_CELL_H;
    CHECK(species_pixels(&c, living, out[0].hue, 0, 0, cw, ch) > 0);
    CHECK(species_pixels(&c, SPECIES_ARMADILLO, out[0].hue, 0, 0, cw, ch) == 0);
    canvas_free(&c);
    scene_free(&sc);
    family_free(&f);
    sim_free(&s);
}

/* The enlarged view is alive: the same body at two moments breathes and
 * blinks, so its pixels differ. Followed or a family's sample, both. */
static void test_enlarged_body_moves(void)
{
    Sim s;
    Families f;
    int slot = make_one_family(&s, &f, 0.5f, 0.0f);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10, 0);
    Canvas a, b;
    CHECK(canvas_init(&a, SCENE_ENLARGED_W, SCENE_ENLARGED_H));
    CHECK(canvas_init(&b, SCENE_ENLARGED_W, SCENE_ENLARGED_H));
    bool followed_moved = false, sample_moved = false;
    FamilySample out[FAMILY_STRIP];
    int n = family_strip(&f, 5, out);
    CHECK(n > 0);
    for (uint64_t t = 100; t < 3000; t += 100) {
        scene_draw_followed(&sc, &a, &s, &s.pool.items[slot], 0, 0, 0);
        scene_draw_followed(&sc, &b, &s, &s.pool.items[slot], t, 0, 0);
        followed_moved = followed_moved || hash_canvas(&a) != hash_canvas(&b);
        scene_draw_sample(&sc, &a, &out[n - 1], 5, 0, 0, 0);
        scene_draw_sample(&sc, &b, &out[n - 1], 5, t, 0, 0);
        sample_moved = sample_moved || hash_canvas(&a) != hash_canvas(&b);
    }
    CHECK(followed_moved);
    CHECK(sample_moved);
    canvas_free(&a);
    canvas_free(&b);
    scene_free(&sc);
    family_free(&f);
    sim_free(&s);
}

/* The largest body fits the enlarged box whole: it drops to scale 1 rather
 * than being cut, so the sky round its edges and above it stays sky. */
static void test_big_body_fits_the_enlarged_box(void)
{
    Sim s;
    make_empty_sim(&s, 32, 32);
    int slot = pool_spawn(&s.pool, &s.world, 5, 5);
    Creature *cr = &s.pool.items[slot];
    cr->genome.g[GENE_ARMOUR] = 1.0f;
    cr->genome.g[GENE_SIZE] = 1.0f;
    cr->grown = 20.0f;
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10, 0);
    Canvas c;
    CHECK(canvas_init(&c, SCENE_ENLARGED_W, SCENE_ENLARGED_H));
    scene_draw_followed(&sc, &c, &s, cr, 0, 0, 0);
    uint32_t sky = lab_sky();
    CHECK(count_not(&c, 0, 0, SCENE_ENLARGED_W, 1, sky) == 0);  /* the top row */
    /* Above the ground and a shadow's height over it, at scale 2 or 1. */
    int above_ground = SCENE_ENLARGED_H - LAB_GROUND_ROWS * 2 - 8;
    CHECK(count_not(&c, 0, 0, 1, above_ground, sky) == 0);     /* the left column */
    CHECK(count_not(&c, SCENE_ENLARGED_W - 1, 0, 1, above_ground, sky) == 0);
    /* And it is there at all. */
    CHECK(count_not(&c, 0, 0, SCENE_ENLARGED_W, above_ground, sky) > 100);
    canvas_free(&c);
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
    s->tick++;
    /* The soil's colours follow the world within a second of window time; a
     * reset draws the ground afresh at once. */
    scene_reset(sc);
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
    s->tick++;
    scene_reset(sc); /* the soil's colours at once, not within a second */
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
    scene_update(&sc, &s, 10, 0);
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
    s->tick++;
    scene_reset(sc); /* the soil's colours at once, not within a second */
    scene_update(sc, s, 10, 0);
    scene_draw_world(sc, c, cam, s, 0.0f, 0, 0, 0, 0);
    const Creature *cr = &s->pool.items[creature];
    /* The World zoom's square is today's colour; the other zooms draw the body
     * in its palette, whose body colours are counted. */
    Colour colour = palette_creature(cr->genome.g[GENE_HUE],
                                     render_meat_share(cr->level));
    SpritePalette pal;
    sprite_palette(&pal, sprite_body(cr, &s->set, SPRITE_BASE_NORMAL).species,
                   cr->genome.g[GENE_HUE]);
    uint32_t body[INK_BELLY - INK_DEEP + 2];
    int kinds = 0;
    body[kinds++] = canvas_rgb(colour.r, colour.g, colour.b);
    for (int k = INK_DEEP; k <= INK_BELLY; k++) {
        body[kinds++] = pal.ink[k];
    }
    *creature_pixels = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        for (int k = 0; k < kinds; k++) {
            if (c->px[i] == body[k]) {
                *creature_pixels += 1;
                break;
            }
        }
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
    tiles_build(a, TILE_RAIN, 0, 16, 0);
    tiles_build(b, TILE_RAIN, 0, 16, 1);
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
    scene_update(&sc, s, 10, 0);
    scene_draw_world(&sc, &a, cam, s, 0.0f, 0, 0, border, border);
    s->pool.items[slot].alive = true;
    scene_update(&sc, s, 10, 0);
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

/* Any body may reach past its cell, by no more than the widest picture and its
 * shadow; a grown one covers more than a cell. Its feet stand on its own cell. */
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
        CHECK(box.right - box.left + 1 <= SPRITE_MAX_W + 2);
        CHECK(box.bottom - box.top + 1 <= SPRITE_MAX_H + 2);
        if (tiles[k] > 1) {
            CHECK(box.right - box.left + 1 > ppc + 2);
        }
        /* Its feet are on its own cell: nothing below that cell's bottom but
         * the edge of its shadow. Its middle is over its own cell. */
        CHECK(box.bottom <= sy + ppc);
        CHECK(box.bottom >= sy + ppc * 13 / 16);
        CHECK(box.left < sx + ppc && box.right >= sx);
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
    scene_update(&sc, &s, 10, 0);
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
        scene_update(&sc, &drawn, 10, (uint64_t)t * 33u);
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
    scene_update(&sc, s, 10, 0);
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

/* ---- Trees, overlays and drawing back to front ---- */

/* A world of bare soil with one plant of this density at (32, 32), at this
 * tick, and a camera at Normal zoom centred on it. */
static void plant_world(Sim *s, Camera *cam, Plant plant, int density,
                        uint64_t tick)
{
    make_empty_sim(s, 64, 64);
    int i = world_idx(&s->world, 32, 32);
    s->world.veg[i] = (uint8_t)density;
    s->world.plant[i] = plant;
    s->world.veg_wither[i] = 0;
    s->tick = tick;
    camera_init(cam, 64, 64, VIEW_W, VIEW_H);
    camera_centre_on(cam, 32.5f, 32.5f);
}

/* One frame of the world, drawn by a scene of its own. */
static void draw_fresh(Sim *s, Canvas *c, const Camera *cam, uint64_t water_ms)
{
    Scene sc;
    CHECK(scene_init(&sc, s));
    scene_update(&sc, s, 10, 0);
    scene_draw_world(&sc, c, cam, s, 0.0f, water_ms, 0, 0, 0);
    scene_free(&sc);
}

static uint32_t pixel_of(Colour c)
{
    return canvas_rgb(c.r, c.g, c.b);
}

/* How many pixels of the canvas, inside the box (right and bottom excluded),
 * are one of these n colours. */
static int count_colours(const Canvas *c, const uint32_t *colours, int n, int left,
                         int top, int right, int bottom)
{
    int count = 0;
    for (int y = top < 0 ? 0 : top; y < bottom && y < c->h; y++) {
        for (int x = left < 0 ? 0 : left; x < right && x < c->w; x++) {
            for (int k = 0; k < n; k++) {
                if (c->px[y * c->w + x] == colours[k]) {
                    count++;
                    break;
                }
            }
        }
    }
    return count;
}

/* The four colours of a healthy crown, as the brief gives them. */
static void crown_colours(const Plant *p, uint32_t out[4])
{
    Colour base = palette_plant(p);
    out[0] = pixel_of(ramp_shade(base, 0.22f));
    out[1] = pixel_of(base);
    out[2] = pixel_of(ramp_shade(base, -0.4f));
    out[3] = pixel_of(ramp_shade(base, -0.25f));
}

static void test_bare_in_autumn(void)
{
    Settings set = settings_default();
    int year = set.ticks_per_year;
    uint64_t summer = (uint64_t)(3 * year + year * 3 / 8);
    uint64_t autumn = (uint64_t)(3 * year + year * 5 / 8);
    uint64_t winter = (uint64_t)(3 * year + year * 7 / 8);
    CHECK(years_season_of(summer, year) == SEASON_SUMMER);
    CHECK(years_season_of(autumn, year) == SEASON_AUTUMN);
    CHECK(years_season_of(winter, year) == SEASON_WINTER);
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    Sim s;
    Plant leaf = {.energy = 5.0f, .fruit_share = 0.0f, .water_need = 0.5f};
    uint32_t crown[4];
    crown_colours(&leaf, crown);
    plant_world(&s, &cam, leaf, VEG_MAX, summer);
    draw_fresh(&s, &c, &cam, 0);
    CHECK(count_colours(&c, crown, 4, 0, 0, VIEW_W, VIEW_H) > 0);
    sim_free(&s);
    plant_world(&s, &cam, leaf, VEG_MAX, autumn);
    draw_fresh(&s, &c, &cam, 0);
    CHECK(count_colours(&c, crown, 4, 0, 0, VIEW_W, VIEW_H) == 0);
    sim_free(&s);
    /* A fruit tree keeps its crown in winter, and shows its fruit. */
    uint32_t fruit = pixel_of(PALETTE_FRUIT);
    Plant laden = {.energy = 5.0f, .fruit_share = 1.0f, .fruit = 2.0f,
                   .water_need = 0.5f};
    plant_world(&s, &cam, laden, VEG_MAX, winter);
    draw_fresh(&s, &c, &cam, 0);
    CHECK(count_colours(&c, &fruit, 1, 0, 0, VIEW_W, VIEW_H) > 0);
    crown_colours(&laden, crown);
    CHECK(count_colours(&c, crown, 4, 0, 0, VIEW_W, VIEW_H) > 0);
    sim_free(&s);
    Plant picked = laden;
    picked.fruit = 0.0f;
    plant_world(&s, &cam, picked, VEG_MAX, winter);
    draw_fresh(&s, &c, &cam, 0);
    CHECK(count_colours(&c, &fruit, 1, 0, 0, VIEW_W, VIEW_H) == 0);
    sim_free(&s);
    canvas_free(&c);
}

static void test_withering_tree_turns_straw(void)
{
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    Sim s;
    Plant leaf = {.energy = 5.0f, .fruit_share = 0.0f, .water_need = 0.5f};
    plant_world(&s, &cam, leaf, VEG_MAX, 200);
    int wither = s.set.veg_wither_ticks / 2;
    s.world.veg_wither[world_idx(&s.world, 32, 32)] = (uint8_t)wither;
    draw_fresh(&s, &c, &cam, 0);
    uint32_t healthy[4];
    crown_colours(&leaf, healthy);
    CHECK(count_colours(&c, healthy, 4, 0, 0, VIEW_W, VIEW_H) == 0);
    float t = (float)palette_wither_shade(wither, s.set.veg_wither_ticks) /
              (float)PALETTE_WITHER_SHADES;
    uint32_t straw = pixel_of(palette_mix(palette_plant(&leaf), PALETTE_WITHERED, t));
    CHECK(count_colours(&c, &straw, 1, 0, 0, VIEW_W, VIEW_H) > 0);
    sim_free(&s);
    canvas_free(&c);
}

/* A big leaf tree at (32, 32) in summer. A giant one row below, tall enough
 * to reach up over the tree's cell, covers the trunk; a body one row above is
 * covered by the crown. */
static void test_trees_sort_with_bodies(void)
{
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    Sim s;
    Plant leaf = {.energy = 5.0f, .fruit_share = 0.0f, .water_need = 0.5f};
    const uint32_t trunk[2] = {canvas_rgb(96, 62, 34), canvas_rgb(70, 44, 24)};
    /* Below: trunk pixels with and without the body. */
    plant_world(&s, &cam, leaf, VEG_MAX, 2000);
    s.world.plant[world_idx(&s.world, 32, 32)].size = s.set.tree_shade_size;
    CHECK(plant_gives_shade(&s.world.plant[world_idx(&s.world, 32, 32)], &s.set));
    int below = place_body(&s, 32, 33, 1000000.0f);
    CHECK(sprite_tiles_for(creature_size(&s.pool.items[below], &s.set)) == 3);
    /* Armoured, large and plant-eating: an elephant or a rhino, tall enough. */
    Genome *g = &s.pool.items[below].genome;
    g->g[GENE_MEAT] = 0.0f;
    g->g[GENE_SWIM] = 0.0f;
    g->g[GENE_ARMOUR] = 1.0f;
    g->g[GENE_SIZE] = 1.0f;
    int kind = sprite_body(&s.pool.items[below], &s.set, SPRITE_BASE_NORMAL).species;
    CHECK(kind == SPECIES_ELEPHANT || kind == SPECIES_RHINO);
    s.tick = 2000; /* summer, and past the body's age */
    s.pool.items[below].alive = false;
    draw_fresh(&s, &c, &cam, 0);
    int bare_trunk = count_colours(&c, trunk, 2, 0, 0, VIEW_W, VIEW_H);
    s.pool.items[below].alive = true;
    draw_fresh(&s, &c, &cam, 0);
    int covered_trunk = count_colours(&c, trunk, 2, 0, 0, VIEW_W, VIEW_H);
    CHECK(bare_trunk > 0);
    CHECK(covered_trunk < bare_trunk);
    sim_free(&s);
    /* Above: the crown reaches into the body's cell, over it. */
    plant_world(&s, &cam, leaf, VEG_MAX, 2000);
    s.world.plant[world_idx(&s.world, 32, 32)].size = s.set.tree_shade_size;
    int above = place_body(&s, 32, 31, 0.0f);
    CHECK(above >= 0);
    s.tick = 2000;
    draw_fresh(&s, &c, &cam, 0);
    int sx, sy;
    camera_to_screen(&cam, 32.0f, 31.0f, &sx, &sy);
    int ppc = camera_ppc(&cam);
    uint32_t crown[4];
    crown_colours(&leaf, crown);
    CHECK(count_colours(&c, crown, 4, sx, sy, sx + ppc, sy + ppc) > 0);
    sim_free(&s);
    canvas_free(&c);
}

/* Paused: the body clock stops, the ambient clock does not. Two of its steps
 * apart, the water has moved and the creature has not. */
static void test_paused_ground_still_moves(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 30; x++) {
            s.world.terrain[y * 64 + x] = TERRAIN_WATER;
        }
    }
    int slot = place_body(&s, 40, 32, 0.0f);
    CHECK(slot >= 0);
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    camera_centre_on(&cam, 32.5f, 32.5f);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10, 0);
    Canvas a, b;
    CHECK(canvas_init(&a, VIEW_W, VIEW_H));
    CHECK(canvas_init(&b, VIEW_W, VIEW_H));
    uint64_t t0 = 1000, t1 = 1000 + 2 * SCENE_AMBIENT_MS;
    CHECK(scene_ambient_frame(t0) != scene_ambient_frame(t1));
    scene_draw_world(&sc, &a, &cam, &s, 0.0f, t0, 500, 0, 0);
    scene_draw_world(&sc, &b, &cam, &s, 0.0f, t1, 500, 0, 0);
    int sx, sy;
    camera_to_screen(&cam, 40.0f, 32.0f, &sx, &sy);
    int wx, wy;
    camera_to_screen(&cam, 30.0f, 0.0f, &wx, &wy);
    int ppc = camera_ppc(&cam);
    int water_moved = 0, body_moved = 0;
    for (int y = 0; y < VIEW_H; y++) {
        for (int x = 0; x < VIEW_W; x++) {
            bool differs = a.px[y * VIEW_W + x] != b.px[y * VIEW_W + x];
            water_moved += differs && x < wx - ppc;
            body_moved += differs && x >= sx && x < sx + ppc && y >= sy && y < sy + ppc;
        }
    }
    CHECK(water_moved > 0);
    CHECK(body_moved == 0);
    canvas_free(&a);
    canvas_free(&b);
    scene_free(&sc);
    sim_free(&s);
}

/* How many pixels of a world of soil at this fertility the overlays drew: the
 * frame against the ground layer under it. Nothing else is in this world. */
static int tuft_pixels(float fertility)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    for (int i = 0; i < 64 * 64; i++) {
        s.world.fertility[i] = fertility;
        s.world.rain[i] = 0.0f; /* rain is an overlay too */
    }
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10, 0);
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    scene_draw_world(&sc, &c, &cam, &s, 0.0f, 0, 0, 0, 0);
    int n = 0;
    CHECK(sc.ground.w == VIEW_W && sc.ground.h == VIEW_H);
    for (int i = 0; i < VIEW_W * VIEW_H && sc.ground.w == VIEW_W; i++) {
        n += c.px[i] != sc.ground.px[i];
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
    return n;
}

static void test_more_tufts_on_fertile_soil(void)
{
    int none = tuft_pixels(0.0f);
    int some = tuft_pixels(0.5f);
    int many = tuft_pixels(1.0f);
    CHECK(none == 0);
    CHECK(some > 0);
    CHECK(many > some);
}

/* A tick no longer redraws the whole ground: carrion shows at once, the soil's
 * colours within a second of window time, and what is drawn then is what a
 * fresh scene draws. */
static void test_ground_follows_within_a_second(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    for (int t = 0; t < 50; t++) {
        sim_step(&s);
    }
    Camera cam;
    camera_init(&cam, 64, 32, VIEW_W, VIEW_H);
    Canvas a, b;
    CHECK(canvas_init(&a, VIEW_W, VIEW_H));
    CHECK(canvas_init(&b, VIEW_W, VIEW_H));
    for (int level = 0; level < CAMERA_WORLD; level++) {
        cam.level = level;
        Scene sc;
        CHECK(scene_init(&sc, &s));
        scene_update(&sc, &s, 10, 0);
        scene_draw_world(&sc, &a, &cam, &s, 0.0f, 0, 0, 0, 0);
        /* Carrion on some soil cells, a tick later: shown now. */
        Sim later;
        CHECK(sim_init(&later, &set, 3, 64, 32));
        for (int t = 0; t < 50; t++) {
            sim_step(&later);
        }
        for (int i = 0; i < 64 * 32; i += 5) {
            if (later.world.terrain[i] == TERRAIN_SOIL) {
                later.world.carrion[i] = 3.0f;
            }
        }
        later.tick++;
        scene_update(&sc, &later, 10, 0);
        scene_draw_world(&sc, &a, &cam, &later, 0.0f, 0, 0, 0, 0);
        draw_fresh(&later, &b, &cam, 0);
        CHECK(hash_canvas(&a) == hash_canvas(&b));
        /* The soil's fertility changes: the layer keeps its colours for now. */
        uint64_t layer = hash_canvas(&sc.ground);
        for (int i = 0; i < 64 * 32; i++) {
            later.world.fertility[i] = 1.0f - later.world.fertility[i];
        }
        later.tick++;
        scene_draw_world(&sc, &a, &cam, &later, 0.0f, 0, 0, 0, 0);
        CHECK(hash_canvas(&sc.ground) == layer);
        /* A second on, with a tick: everything shows, as a fresh scene draws it. */
        later.tick++;
        scene_draw_world(&sc, &a, &cam, &later, 0.0f, SCENE_COLOUR_MS, 0, 0, 0);
        draw_fresh(&later, &b, &cam, SCENE_COLOUR_MS);
        CHECK(hash_canvas(&a) == hash_canvas(&b));
        scene_free(&sc);
        sim_free(&later);
    }
    canvas_free(&a);
    canvas_free(&b);
    sim_free(&s);
}

/* How many pixels of two canvases of the view's size differ. */
static int pixels_differing(const Canvas *a, const Canvas *b)
{
    int n = 0;
    for (int p = 0; p < VIEW_W * VIEW_H; p++) {
        n += a->px[p] != b->px[p];
    }
    return n;
}

/* A world of soil with a small lake, every cell alike, nothing on it. */
static void lake_world(Sim *s, int w, int h)
{
    make_empty_sim(s, w, h);
    for (int i = 0; i < w * h; i++) {
        s->world.fertility[i] = 0.3f;
        s->world.water[i] = 0.3f;
        s->world.snow[i] = 0.0f;
        s->world.rain[i] = 0.0f;
    }
    for (int y = 2; y < 5; y++) {
        for (int x = 2; x < 6; x++) {
            s->world.terrain[y * w + x] = TERRAIN_WATER;
        }
    }
}

enum { CHANGE_FERTILITY, CHANGE_SNOW, CHANGE_WATER, CHANGE_CARRION, CHANGES };

/*
 * The ruling, cell by cell: a few single cells change (the world's four
 * corners among them, whose tiles wrap) on a tick. Carrion shows at once; a
 * soil colour change is not shown before a second of window time and, after
 * it, the frame is exactly what a fresh scene draws. In a world larger than
 * the view and in one smaller (whole, on black), at Close, Normal and Far.
 */
static void test_soil_colour_follows_cell_by_cell(void)
{
    const int sizes[2][2] = {{64, 32}, {12, 7}};
    Canvas a, b;
    CHECK(canvas_init(&a, VIEW_W, VIEW_H));
    CHECK(canvas_init(&b, VIEW_W, VIEW_H));
    for (int w = 0; w < 2; w++) {
        int ww = sizes[w][0], wh = sizes[w][1];
        const int cells[6] = {0, ww - 1, (wh - 1) * ww, ww * wh - 1,
                              ww * (wh / 2) + ww / 2, 37 % (ww * wh)};
        for (int change = 0; change < CHANGES; change++) {
            for (int level = 0; level < CAMERA_WORLD; level++) {
                Sim s;
                lake_world(&s, ww, wh);
                Camera cam;
                camera_init(&cam, ww, wh, VIEW_W, VIEW_H);
                cam.level = level;
                Scene sc;
                CHECK(scene_init(&sc, &s));
                scene_update(&sc, &s, 10, 0);
                scene_draw_world(&sc, &a, &cam, &s, 0.0f, 0, 0, 0, 0);
                for (int k = 0; k < 6; k++) {
                    int i = cells[k];
                    s.world.fertility[i] = change == CHANGE_FERTILITY ? 1.0f : 0.3f;
                    s.world.snow[i] = change == CHANGE_SNOW ? 1.0f : 0.0f;
                    s.world.water[i] = change == CHANGE_WATER ? 1.0f : 0.3f;
                    s.world.carrion[i] = change == CHANGE_CARRION ? 3.0f : 0.0f;
                }
                s.tick++;
                scene_draw_world(&sc, &a, &cam, &s, 0.0f, 400, 0, 0, 0);
                draw_fresh(&s, &b, &cam, 400);
                if (change == CHANGE_CARRION) {
                    CHECK(pixels_differing(&a, &b) == 0); /* at once */
                    for (int k = 0; k < 6; k++) {
                        s.world.carrion[cells[k]] = 0.0f;
                    }
                    s.tick++;
                    scene_draw_world(&sc, &a, &cam, &s, 0.0f, 450, 0, 0, 0);
                    draw_fresh(&s, &b, &cam, 450);
                    CHECK(pixels_differing(&a, &b) == 0); /* gone at once */
                } else {
                    CHECK(pixels_differing(&a, &b) > 0); /* not yet */
                }
                s.tick++;
                scene_draw_world(&sc, &a, &cam, &s, 0.0f, 1100, 0, 0, 0);
                draw_fresh(&s, &b, &cam, 1100);
                CHECK(pixels_differing(&a, &b) == 0);
                scene_free(&sc);
                sim_free(&s);
            }
        }
    }
    canvas_free(&a);
    canvas_free(&b);
}

/* Paused (or the world over) right after a tick that changed the soil: no
 * tick follows, and the colours must still show once the second has passed. */
static void test_paused_soil_colour_catches_up(void)
{
    Canvas a, b;
    CHECK(canvas_init(&a, VIEW_W, VIEW_H));
    CHECK(canvas_init(&b, VIEW_W, VIEW_H));
    for (int level = 0; level < CAMERA_WORLD; level++) {
        Sim s;
        lake_world(&s, 64, 32);
        Camera cam;
        camera_init(&cam, 64, 32, VIEW_W, VIEW_H);
        cam.level = level;
        Scene sc;
        CHECK(scene_init(&sc, &s));
        scene_update(&sc, &s, 10, 0);
        scene_draw_world(&sc, &a, &cam, &s, 0.0f, 0, 0, 0, 0);
        for (int i = 0; i < 64 * 32; i++) {
            s.world.fertility[i] = 1.0f - s.world.fertility[i];
        }
        s.tick++;
        scene_draw_world(&sc, &a, &cam, &s, 0.0f, 300, 0, 0, 0);
        draw_fresh(&s, &b, &cam, 300);
        CHECK(pixels_differing(&a, &b) > 0); /* not yet */
        /* Paused: the window time goes on, a frame every ambient step. */
        for (uint64_t ms = 400; ms <= 1400; ms += SCENE_AMBIENT_MS) {
            scene_draw_world(&sc, &a, &cam, &s, 0.0f, ms, 0, 0, 0);
        }
        draw_fresh(&s, &b, &cam, 1400);
        CHECK(pixels_differing(&a, &b) == 0);
        scene_free(&sc);
        sim_free(&s);
    }
    canvas_free(&a);
    canvas_free(&b);
}

/* A frame of an unchanged world builds no picture: everything it needs was
 * drawn the frame before. At every zoom with bodies. */
static void test_second_frame_builds_nothing(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    for (int t = 0; t < 50; t++) {
        sim_step(&s);
    }
    Scene sc;
    CHECK(scene_init(&sc, &s));
    scene_update(&sc, &s, 10, 0);
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 64, 32, VIEW_W, VIEW_H);
    for (int level = 0; level < CAMERA_WORLD; level++) {
        cam.level = level;
        long before = sc.sprites.builds;
        scene_draw_world(&sc, &c, &cam, &s, 0.5f, 1000, 1000, 0, 0);
        long first = sc.sprites.builds;
        CHECK(first > before);
        scene_draw_world(&sc, &c, &cam, &s, 0.5f, 1000, 1000, 0, 0);
        CHECK(sc.sprites.builds == first);
    }
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

/* The pixels of the canvas that differ from the empty picture. */
static void diff_from(const Canvas *c, const Canvas *empty, bool *out)
{
    for (int i = 0; i < c->w * c->h; i++) {
        out[i] = c->px[i] != empty->px[i];
    }
}

/* Two bodies one row apart, overlapping: the lower one is drawn in front,
 * though it holds the earlier pool slot. */
static void test_bodies_sort_by_row(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    /* Giants: tall enough to reach over the row above. */
    int lower = place_body(&s, 32, 33, 1000000.0f);
    int upper = place_body(&s, 32, 32, 1000000.0f);
    CHECK(lower >= 0 && upper >= 0 && lower < upper);
    s.pool.items[lower].genome.g[GENE_HUE] = 0.0f;
    s.pool.items[upper].genome.g[GENE_HUE] = 0.6f;
    Camera cam;
    camera_init(&cam, 64, 64, VIEW_W, VIEW_H);
    camera_centre_on(&cam, 32.5f, 32.5f);
    Canvas empty, only_upper, only_lower, both;
    CHECK(canvas_init(&empty, VIEW_W, VIEW_H));
    CHECK(canvas_init(&only_upper, VIEW_W, VIEW_H));
    CHECK(canvas_init(&only_lower, VIEW_W, VIEW_H));
    CHECK(canvas_init(&both, VIEW_W, VIEW_H));
    s.pool.items[lower].alive = false;
    s.pool.items[upper].alive = false;
    draw_fresh(&s, &empty, &cam, 0);
    s.pool.items[upper].alive = true;
    draw_fresh(&s, &only_upper, &cam, 0);
    s.pool.items[upper].alive = false;
    s.pool.items[lower].alive = true;
    draw_fresh(&s, &only_lower, &cam, 0);
    s.pool.items[upper].alive = true;
    draw_fresh(&s, &both, &cam, 0);
    SpritePalette pal;
    sprite_palette(&pal, sprite_body(&s.pool.items[lower], &s.set, SPRITE_BASE_NORMAL).species,
                   0.0f);
    /* static: two blocks of the view's size, too large for the stack. */
    static bool by_upper[VIEW_W * VIEW_H], by_lower[VIEW_W * VIEW_H];
    diff_from(&only_upper, &empty, by_upper);
    diff_from(&only_lower, &empty, by_lower);
    int overlap = 0, wrong = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        bool lower_body = false;
        for (int k = INK_DEEP; k < INK_COUNT; k++) {
            lower_body = lower_body || only_lower.px[i] == pal.ink[k];
        }
        if (by_upper[i] && by_lower[i] && lower_body) {
            overlap++;
            wrong += both.px[i] != only_lower.px[i];
        }
    }
    CHECK(overlap > 0);
    CHECK(wrong == 0);
    canvas_free(&empty);
    canvas_free(&only_upper);
    canvas_free(&only_lower);
    canvas_free(&both);
    sim_free(&s);
}

/* ---- Particles ---- */

/* How many particles of the pool have this colour, within `cells` cells
 * (16 art pixels each) of cell (cx, cy)'s middle; and how many have it at all. */
static int particles_near(const Scene *sc, bool (*is)(uint32_t), int cx, int cy,
                          float cells, int *all)
{
    int near = 0;
    *all = 0;
    for (int i = 0; i < sc->particles.count; i++) {
        const Particle *p = &sc->particles.p[i];
        if (!is(p->colour)) {
            continue;
        }
        (*all)++;
        float x, y;
        particle_position(p, p->age, &x, &y);
        float dx = x / 16.0f - ((float)cx + 0.5f);
        float dy = y / 16.0f - ((float)cy + 0.5f);
        near += fabsf(dx) <= cells && fabsf(dy) <= cells;
    }
    return near;
}

/* The blood's colours (v9:689): red 176..216, green 28, blue 40. */
static bool is_blood(uint32_t px)
{
    uint32_t r = (px >> 16) & 0xFFu, g = (px >> 8) & 0xFFu, b = px & 0xFFu;
    return r >= 176u && r <= 216u && g == 28u && b == 40u;
}

static bool is_dust(uint32_t px)
{
    return px == canvas_rgb(176, 160, 128);
}

static bool is_z(uint32_t px)
{
    return px == canvas_rgb(220, 230, 255);
}

/* At the bite (the attack at 0.5, 28 frames in) seven drops of blood fly from
 * the victim, and not before; they are drawn red on the canvas. */
static void test_blood_on_a_hit(void)
{
    Sim s;
    int hunter, prey;
    make_fight(&s, ACT_ATTACK, &hunter, &prey);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 16, 16, VIEW_W, VIEW_H);
    uint64_t ms = 0;
    int all = 0;
    for (int i = 0; i <= 31; i++) {
        scene_update(&sc, &s, 10, ms);
        scene_draw_world(&sc, &c, &cam, &s, 0.0f, ms, ms, 0, 0);
        if (i == 20) {
            particles_near(&sc, is_blood, 9, 8, 2.0f, &all);
            CHECK(all == 0); /* still stalking */
        }
        ms += 33u;
    }
    int near = particles_near(&sc, is_blood, 9, 8, 2.0f, &all);
    CHECK(all == 7);
    CHECK(near == 7);
    if (all != 7) {
        printf("  blood: %d particles, %d near the victim\n", all, near);
    }
    int red = 0;
    for (int i = 0; i < VIEW_W * VIEW_H; i++) {
        red += is_blood(c.px[i]);
    }
    CHECK(red > 0);
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

/* A sleeper sends up a Z every 1.2 s, from above its body; a resting
 * creature none. */
static void test_zs_over_sleepers(void)
{
    Sim s;
    int sleeper, rester;
    make_fight(&s, ACT_SLEEP, &sleeper, &rester);
    s.pool.items[rester].current.kind = ACT_REST;
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, 16, 16, VIEW_W, VIEW_H);
    int most = 0, above = 0;
    for (int i = 0; i < 90; i++) { /* 3 s */
        uint64_t ms = 1000u + (uint64_t)i * 33u;
        scene_update(&sc, &s, 10, ms);
        scene_draw_world(&sc, &c, &cam, &s, 0.0f, ms, ms, 0, 0);
        int count = 0;
        for (int k = 0; k < sc.particles.count; k++) {
            const Particle *p = &sc.particles.p[k];
            if (p->kind != PARTICLE_Z || !is_z(p->colour)) {
                continue;
            }
            count++;
            float x, y;
            particle_position(p, p->age, &x, &y);
            /* Over the sleeper's cell (8, 8), above its feet. */
            above += x >= 8.0f * 16.0f - 8.0f && x < 9.0f * 16.0f + 8.0f && y < 8.0f * 16.0f + 13.0f;
        }
        most = count > most ? count : most;
    }
    CHECK(most >= 1 && most <= 2); /* a 1.6 s life, one every 1.2 s */
    CHECK(above > 0);
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

/* Dust rises behind a walker in view, and only there: a walker far off the
 * view sends up none. The walk is set in the drawing's memory by hand, a
 * glide from the next cell west that never ends. */
static void test_dust_only_in_view(void)
{
    Sim s;
    make_empty_sim(&s, 64, 64);
    int seen = pool_spawn(&s.pool, &s.world, 10, 10);
    int unseen = pool_spawn(&s.pool, &s.world, 40, 40);
    CHECK(seen >= 0 && unseen >= 0);
    Scene sc;
    CHECK(scene_init(&sc, &s));
    Canvas c;
    CHECK(canvas_init(&c, 160, 96));
    Camera cam;
    camera_init(&cam, 64, 64, 160, 96);
    cam.level = CAMERA_NORMAL;
    camera_centre_on(&cam, 10.5f, 10.5f);
    int slots[2] = {seen, unseen};
    int sum_near = 0, sum_all = 0;
    for (int i = 0; i < 60; i++) { /* 2 s */
        uint64_t ms = 1000u + (uint64_t)i * 33u;
        scene_update(&sc, &s, 10, ms);
        for (int k = 0; k < 2; k++) {
            AnimSlot *m = &sc.anim.slots[slots[k]];
            m->from_x = m->x - 1;
            m->moved_tick = s.tick;
            m->move_ticks = 1000.0f;
        }
        scene_draw_world(&sc, &c, &cam, &s, 0.5f, ms, ms, 0, 0);
        CHECK(sc.poses[seen].action == ACTION_WALK);
        /* A puff every 0.4 s living 0.4 s: one at a time, so count each
         * frame's. */
        int all;
        sum_near += particles_near(&sc, is_dust, 10, 10, 2.0f, &all);
        sum_all += all;
    }
    CHECK(sum_all > 20);
    CHECK(sum_near == sum_all);
    canvas_free(&c);
    scene_free(&sc);
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
    test_fight_draws_an_attack();
    test_click_picks_within_two_cells();
    test_click_finds_across_the_wrapped_edge();
    test_follow_centres_on_the_creature();
    test_followed_creature_dies_camera_stays();
    test_new_world_lets_go();
    test_follow_across_the_edge_takes_the_short_way();
    test_layout_gives_the_panel_way_first();
    test_strip_and_enlarged_bodies();
    test_strip_shows_the_family_species();
    test_enlarged_body_moves();
    test_big_body_fits_the_enlarged_box();
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
    test_bare_in_autumn();
    test_withering_tree_turns_straw();
    test_trees_sort_with_bodies();
    test_second_frame_builds_nothing();
    test_bodies_sort_by_row();
    test_paused_ground_still_moves();
    test_more_tufts_on_fertile_soil();
    test_ground_follows_within_a_second();
    test_soil_colour_follows_cell_by_cell();
    test_paused_soil_colour_catches_up();
    test_blood_on_a_hit();
    test_zs_over_sleepers();
    test_dust_only_in_view();
    TEST_SUMMARY();
}
