/*
 * slow_pictures: whether the picture cache keeps up with a real world. Loads
 * the lightness save and draws 600 frames of it at Normal into a 1280 by 720
 * canvas, with no window: 10 ticks a second (the window's default until
 * 2026-11-09, kept so the numbers stay comparable with older runs), 33 ms of
 * body time a frame, as the window runs. Over the last 540 frames (the first 60 fill the cache) it
 * prints how many pictures were built per second and how long a frame's
 * drawing took, and fails if any one frame built more than 500 pictures: a
 * cache that redraws most bodies every frame.
 *
 * The world: the save named by $AGON_LOOKS_SAVE, or else one made here the
 * way the lightness save was made (seed 3, 256 by 144, run for 300 ticks:
 * about 530 creatures). Made here, it cannot go stale: a save file stops
 * loading whenever the save format's version moves on.
 * Times mean something only from a release build: make MODE=release run-slow-tests.
 */
#include "test.h"

#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* getenv */
#include <time.h>   /* clock_gettime */

#include "camera.h"
#include "canvas.h"
#include "save.h"
#include "scene.h"
#include "settings.h"
#include "sim.h"

#define FRAMES 600
#define WARM_UP 60
#define VIEW_W 1280
#define VIEW_H 720
#define TICKS_PER_SECOND 10
#define FRAMES_PER_SECOND 30
#define BODY_MS_PER_FRAME 33u
#define MOST_BUILDS_A_FRAME 500
#define MADE_SEED 3
#define MADE_W 256
#define MADE_H 144
#define MADE_TICKS 300

/* A clock in milliseconds that only goes forwards. */
static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

/* The world to draw: loaded from path, or made here when path is NULL. */
static bool get_world(Sim *s, const char *path)
{
    if (path == NULL) {
        Settings set = settings_default();
        if (!sim_init(s, &set, MADE_SEED, MADE_W, MADE_H)) {
            fprintf(stderr, "slow_pictures: cannot make the world\n");
            return false;
        }
        for (int i = 0; i < MADE_TICKS; i++) {
            sim_step(s);
        }
        return true;
    }
    char err[256];
    if (!save_read(s, path, err, sizeof(err))) {
        fprintf(stderr, "slow_pictures: cannot load %s: %s\n", path, err);
        return false;
    }
    return true;
}

static void test_cache_keeps_up(const char *path)
{
    Sim s;
    if (!get_world(&s, path)) {
        CHECK(false);
        return;
    }
    Scene sc;
    Canvas c;
    CHECK(scene_init(&sc, &s));
    CHECK(canvas_init(&c, VIEW_W, VIEW_H));
    Camera cam;
    camera_init(&cam, s.world.w, s.world.h, VIEW_W, VIEW_H); /* Normal */
    int ticks_due = 0;
    uint64_t clock_ms = 0;
    long built = 0, most = 0;
    double drawing_ms = 0.0;
    for (int f = 0; f < FRAMES; f++) {
        /* As the window does: a frame adds the speed, a tick spends 30. */
        ticks_due += TICKS_PER_SECOND;
        while (ticks_due >= FRAMES_PER_SECOND && !sim_is_over(&s)) {
            sim_step(&s);
            ticks_due -= FRAMES_PER_SECOND;
        }
        clock_ms += BODY_MS_PER_FRAME;
        scene_update(&sc, &s, TICKS_PER_SECOND, clock_ms);
        float fraction = (float)ticks_due / (float)FRAMES_PER_SECOND;
        long before = sc.sprites.builds;
        double start = now_ms();
        scene_draw_world(&sc, &c, &cam, &s, fraction, clock_ms, clock_ms, 0, 0);
        double took = now_ms() - start;
        long this_frame = sc.sprites.builds - before;
        if (f >= WARM_UP) {
            built += this_frame;
            most = this_frame > most ? this_frame : most;
            drawing_ms += took;
        }
    }
    int measured = FRAMES - WARM_UP;
    double seconds = (double)measured / (double)FRAMES_PER_SECOND;
    printf("slow_pictures: %d creatures, over the last %d frames (%.0f s of window time):\n",
           s.pool.count, measured, seconds);
    printf("  pictures built per second %.1f (%ld in all, at most %ld in one frame)\n",
           (double)built / seconds, built, most);
    printf("  ms per frame %.2f (drawing the world only)\n", drawing_ms / (double)measured);
    CHECK(most <= MOST_BUILDS_A_FRAME);
    canvas_free(&c);
    scene_free(&sc);
    sim_free(&s);
}

int main(void)
{
    const char *chosen = getenv("AGON_LOOKS_SAVE");
    test_cache_keeps_up(chosen != NULL && chosen[0] != '\0' ? chosen : NULL);
    TEST_SUMMARY();
}
