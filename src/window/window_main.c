/*
 * agon-window: the simulation in a window. One function, app_frame, does
 * one frame; platform_run calls it. There is no loop here, because a browser
 * has to own the loop.
 */
#include <inttypes.h> /* PRIx64 */
#include <stdio.h>    /* snprintf, printf, fputs */
#include <string.h>   /* strcmp */
#include <time.h>     /* time */

#include "app.h"
#include "camera.h"
#include "canvas.h"
#include "cells.h"
#include "cells_canvas.h"
#include "font.h"
#include "options.h"
#include "panel.h" /* panel_format_count */
#include "platform.h"
#include "rng.h"
#include "save.h"
#include "scene.h"
#include "settings.h"
#include "sim.h"

#define FRAMES_PER_SECOND 30
#define WINDOW_W 1280
#define WINDOW_H 720
#define DEFAULT_WORLD_W 256
#define DEFAULT_WORLD_H 144
/* The title line's height, above the world. */
#define TITLE_H FONT_H
#define NOTICE_FRAMES (2 * FRAMES_PER_SECOND)
/* How many screen pixels an arrow key pans by. */
#define ARROW_PAN 48

static const char WINDOW_USAGE[] =
    "usage: agon-window [options]\n"
    "  --seed N          the world's seed\n"
    "  --size WxH        world size in cells (default 256x144)\n"
    "  --speed N         ticks per second (default 10)\n"
    "  --no-floor        never place new random creatures\n"
    "  --config PATH     read settings from a file\n"
    "  --save PATH       write the world there on quit and on the s key\n"
    "  --load PATH       resume a saved world\n"
    "  --zoom NAME       start at close, normal, far or world\n"
    "  --checksum        print a fingerprint of the world on leaving\n"
    "  --frames N        stop after N frames, without waiting between them\n"
    "  --screenshot PATH write the last frame there as a PPM image\n"
    "  -h, --help        this text\n"
    "  --version         the version\n"
    "keys: space pause, +/- speed, r new seed, s save, q quit, arrows move,\n"
    "      [ and ] zoom. mouse: wheel zooms, dragging moves.\n";

/* Everything a frame needs, passed through platform_run as a void *. */
typedef struct {
    Sim sim;
    Scene scene;
    Canvas canvas;
    Camera camera;
    const Options *opt;
    Rng seed_source;
    int speed;
    int ticks_due; /* ticks owed, in thirtieths, as in the terminal program */
    bool paused, running, failed;
    bool dragging;
    int mouse_x, mouse_y;
    long frames_done;
    int notice_frames;
    char notice[256];
    uint64_t body_ms;  /* a clock that stops while paused */
    uint64_t last_ms;
} App;

/* The world's part of the window: all of it below the title line. */
static void apply_window_size(App *a, int w, int h)
{
    /* If memory runs out the old canvas stays, and is simply stretched. */
    if (canvas_resize(&a->canvas, w, h)) {
        camera_resize(&a->camera, w, h - TITLE_H);
    }
}

static void new_world(App *a)
{
    if (!sim_reseed(&a->sim, rng_next(&a->seed_source))) {
        a->failed = true; /* the brain could not be reopened */
        a->running = false;
        return;
    }
    scene_reset(&a->scene);
}

static void handle_key(App *a, int key)
{
    switch (key) {
    case 'q':
        a->running = false;
        break;
    case ' ':
        a->paused = !a->paused;
        break;
    case '+':
    case '=': /* the same key as + on most keyboards, without shift */
        if (a->speed < APP_SPEED_COUNT - 1) {
            a->speed++;
        }
        break;
    case '-':
        if (a->speed > 0) {
            a->speed--;
        }
        break;
    case 'r':
        new_world(a);
        break;
    case 's':
        app_save_now(&a->sim, a->opt->save_path, a->notice, sizeof(a->notice));
        a->notice_frames = NOTICE_FRAMES;
        break;
    case ']':
        camera_zoom(&a->camera, 1, a->camera.view_w / 2, a->camera.view_h / 2);
        break;
    case '[':
        camera_zoom(&a->camera, -1, a->camera.view_w / 2, a->camera.view_h / 2);
        break;
    case KEY_LEFT: camera_pan(&a->camera, -ARROW_PAN, 0); break;
    case KEY_RIGHT: camera_pan(&a->camera, ARROW_PAN, 0); break;
    case KEY_UP: camera_pan(&a->camera, 0, -ARROW_PAN); break;
    case KEY_DOWN: camera_pan(&a->camera, 0, ARROW_PAN); break;
    default: break;
    }
}

static void handle_event(App *a, const Event *e)
{
    switch (e->kind) {
    case EV_QUIT:
        a->running = false;
        break;
    case EV_KEY:
        handle_key(a, e->key);
        break;
    case EV_WHEEL:
        /* The camera counts from the view's corner, below the title line. */
        camera_zoom(&a->camera, e->delta, e->x, e->y - TITLE_H);
        break;
    case EV_DOWN:
        a->dragging = true;
        a->mouse_x = e->x;
        a->mouse_y = e->y;
        break;
    case EV_UP:
        a->dragging = false;
        break;
    case EV_MOVE:
        if (a->dragging) {
            /* Dragging pulls the world along: the view moves the other way. */
            camera_pan(&a->camera, a->mouse_x - e->x, a->mouse_y - e->y);
            a->mouse_x = e->x;
            a->mouse_y = e->y;
        }
        break;
    case EV_RESIZE:
        apply_window_size(a, e->w, e->h);
        break;
    }
}

static void draw_title(App *a)
{
    char tick[32];
    panel_format_count(tick, sizeof(tick), (long)a->sim.tick);
    char text[400];
    snprintf(text, sizeof(text),
             " agon | seed 0x%" PRIx64 " | tick %s | creatures %d | %d/s%s%s%s",
             a->sim.seed, tick, a->sim.pool.count, APP_SPEEDS[a->speed],
             a->paused ? " | paused" : "", a->notice[0] != '\0' ? " | " : "",
             a->notice);
    render_text(0, 0, text, a->canvas.w / FONT_W + 1);
}

static void advance_clocks(App *a)
{
    uint64_t now = platform_now_ms();
    if (!a->paused) {
        a->body_ms += now - a->last_ms;
    }
    a->last_ms = now;
    if (a->notice_frames > 0 && --a->notice_frames == 0) {
        a->notice[0] = '\0';
    }
}

/* One frame. void * because platform_run knows nothing about App. */
static void app_frame(void *arg)
{
    App *a = arg; /* in C, void * converts to any pointer without a cast */
    Event e;
    while (platform_poll(&e)) {
        handle_event(a, &e);
    }
    /* Before any ticks: a quit means the world stays as it was shown. */
    if (!a->running) {
        platform_stop();
        return;
    }
    if (!a->paused) {
        a->ticks_due += APP_SPEEDS[a->speed];
        while (a->ticks_due >= FRAMES_PER_SECOND) {
            sim_step(&a->sim);
            a->ticks_due -= FRAMES_PER_SECOND;
        }
    }
    scene_update(&a->scene, &a->sim, APP_SPEEDS[a->speed]);
    advance_clocks(a);

    float fraction = (float)a->ticks_due / (float)FRAMES_PER_SECOND;
    scene_draw_world(&a->scene, &a->canvas, &a->camera, &a->sim, fraction,
                     a->last_ms, a->body_ms, 0, TITLE_H);
    draw_title(a);
    platform_present(&a->canvas);

    /* The same way out as the window's X button, so --frames tests it. */
    a->frames_done++;
    if (a->opt->frames > 0 && a->frames_done == a->opt->frames) {
        platform_post_quit();
    }
}

/* The zoom step with this name; false when there is none. */
static bool zoom_from_name(const char *name, int *level)
{
    static const char *const NAMES[CAMERA_LEVELS] = {"close", "normal", "far",
                                                     "world"};
    for (int i = 0; i < CAMERA_LEVELS; i++) {
        if (strcmp(name, NAMES[i]) == 0) {
            *level = i;
            return true;
        }
    }
    return false;
}

/* A seed for when none is given: the clock, mixed. time() has only
 * seconds, which is enough for a program a person starts by hand. */
static uint64_t seed_from_clock(void)
{
    uint64_t state = (uint64_t)time(NULL);
    return rng_splitmix64(&state);
}

/* Settings from the file and options; false with err filled in. */
static bool read_settings(const Options *o, Settings *settings, char *err,
                          size_t errlen)
{
    *settings = settings_default();
    if (o->config_path != NULL &&
        !settings_load(settings, o->config_path, err, errlen)) {
        return false;
    }
    if (o->no_floor) {
        settings->min_creatures = 0;
    }
    return true;
}

/* What happens on leaving, in the order that keeps a failure visible. */
static int app_leave(App *a)
{
    int result = EXIT_FINE;
    char err[512];
    if (a->failed) {
        platform_error("out of memory while building a new world");
        result = EXIT_FAILED;
    }
    if (a->opt->screenshot_path != NULL &&
        !canvas_write_ppm(&a->canvas, a->opt->screenshot_path)) {
        platform_error("could not write the screenshot");
        result = EXIT_FAILED;
    }
    if (!a->failed && a->opt->save_path != NULL &&
        !save_write(&a->sim, a->opt->save_path, err, sizeof(err))) {
        platform_error(err);
        result = EXIT_FAILED;
    }
    if (a->opt->checksum) {
        printf("checksum 0x%016" PRIx64 "\n", sim_checksum(&a->sim));
    }
    return result;
}

int main(int argc, char **argv)
{
    Options opt;
    char err[512];
    if (!options_parse(argc, argv, &opt, err, sizeof(err))) {
        fprintf(stderr, "agon-window: %s (see agon-window --help)\n", err);
        return EXIT_USAGE;
    }
    if (opt.help) {
        fputs(WINDOW_USAGE, stdout);
        return EXIT_FINE;
    }
    if (opt.version) {
        printf("agon-window %s\n", AGON_VERSION);
        return EXIT_FINE;
    }
    if (opt.once || opt.list_settings) {
        fprintf(stderr, "agon-window: --once and --settings are for agon\n");
        return EXIT_USAGE;
    }
    int zoom = CAMERA_NORMAL;
    if (opt.zoom_name != NULL && !zoom_from_name(opt.zoom_name, &zoom)) {
        fprintf(stderr, "agon-window: --zoom needs close, normal, far or "
                        "world\n");
        return EXIT_USAGE;
    }
    Settings settings;
    if (!read_settings(&opt, &settings, err, sizeof(err))) {
        fprintf(stderr, "agon-window: %s\n", err);
        return EXIT_USAGE;
    }

    /* static: this big struct lives for the whole program, not on main's
     * stack, and starts as all zeros. */
    static App app;
    app.opt = &opt;
    uint64_t seed = opt.seed_given ? opt.seed : seed_from_clock();
    int world_w = opt.size_given ? opt.width : DEFAULT_WORLD_W;
    int world_h = opt.size_given ? opt.height : DEFAULT_WORLD_H;
    if (!app_open_world(&app.sim, &opt, &settings, seed, world_w, world_h, err,
                        sizeof(err))) {
        platform_error(err);
        return EXIT_FAILED;
    }
    rng_seed(&app.seed_source, app.sim.seed);
    app.speed = opt.speed > 0 ? app_speed_step(opt.speed) : APP_DEFAULT_SPEED;
    app.running = true;

    if (!scene_init(&app.scene, &app.sim) ||
        !canvas_init(&app.canvas, WINDOW_W, WINDOW_H)) {
        platform_error("out of memory");
        return EXIT_FAILED;
    }
    cells_canvas_bind(&app.canvas);
    camera_init(&app.camera, app.sim.world.w, app.sim.world.h, WINDOW_W,
                WINDOW_H - TITLE_H);
    app.camera.level = zoom;
    if (!platform_open("agon", WINDOW_W, WINDOW_H, err, sizeof(err))) {
        platform_error(err);
        return EXIT_FAILED;
    }
    app.last_ms = platform_now_ms();

    platform_run(app_frame, &app, opt.frames > 0 ? 0 : FRAMES_PER_SECOND);

    int result = app_leave(&app);
    platform_close();
    canvas_free(&app.canvas);
    scene_free(&app.scene);
    sim_free(&app.sim);
    return result;
}
