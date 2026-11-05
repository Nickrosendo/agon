/*
 * agon-window: the simulation in a window. One function, app_frame, does
 * one frame; platform_run calls it. There is no loop here, because a browser
 * has to own the loop.
 */
#include <inttypes.h> /* PRIx64 */
#include <stdio.h>    /* snprintf, printf, fputs */
#include <stdlib.h>   /* abs */
#include <string.h>   /* strcmp */
#include <time.h>     /* time */

#include "app.h"
#include "camera.h"
#include "canvas.h"
#include "cells.h"
#include "cells_canvas.h"
#include "family.h"
#include "font.h"
#include "inspect.h"
#include "options.h"
#include "palette.h"
#include "panel.h" /* panel_format_count */
#include "platform.h"
#include "rng.h"
#include "save.h"
#include "scene.h"
#include "settings.h"
#include "sim.h"
#include "years.h"

#define FRAMES_PER_SECOND 30
#define WINDOW_W 1280
#define WINDOW_H 720
/* A press that moves further than this, in pixels, is a drag, not a click. */
#define CLICK_SLOP 4
#define DEFAULT_WORLD_W 256
#define DEFAULT_WORLD_H 144
#define NOTICE_FRAMES (2 * FRAMES_PER_SECOND)
/* How many screen pixels an arrow key pans by. */
#define ARROW_PAN 48

static const char WINDOW_USAGE[] =
    "usage: agon-window [options]\n"
    "  --seed N          the world's seed\n"
    "  --size WxH        world size in cells (default 256x144)\n"
    "  --speed N         ticks per second (default 10)\n"
    "  --no-floor        turn off a floor a settings file set\n"
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
    "      [ and ] zoom, Esc lets go of a creature, n new world after game over.\n"
    "mouse: wheel zooms, dragging moves, clicking a creature follows it,\n"
    "       clicking bare ground lets go. On the panel, clicking \"top hunter\",\n"
    "       \"oldest\" or the largest family's body follows that creature.\n";

/* Everything a frame needs, passed through platform_run as a void *. */
typedef struct {
    Sim sim;
    Scene scene;
    Families families;
    Canvas canvas;
    Camera camera;
    Layout layout;
    Inspect inspect;
    const Options *opt;
    Rng seed_source;
    int speed;
    int ticks_due; /* ticks owed, in thirtieths, as in the terminal program */
    bool paused, running, failed;
    bool redraw;    /* while paused: something happened that would show */
    int last_water; /* the water picture in the last frame drawn */
    bool pressed;  /* the left button went down over the world */
    bool dragging; /* and has moved far enough not to be a click */
    int press_x, press_y;
    int mouse_x, mouse_y;
    long frames_done;
    int notice_frames;
    char notice[256];
    uint64_t body_ms;  /* a clock that stops while paused */
    uint64_t last_ms;
} App;

static void apply_window_size(App *a, int w, int h)
{
    /* If memory runs out the old canvas stays, and is simply stretched. */
    if (canvas_resize(&a->canvas, w, h)) {
        scene_layout(&a->layout, w, h);
        camera_resize(&a->camera, a->layout.view_w, a->layout.view_h);
    }
}

/* True when this window pixel is on the world's part of the window. */
static bool over_view(const App *a, int x, int y)
{
    const Layout *l = &a->layout;
    return x >= l->view_x && x < l->view_x + l->view_w && y >= l->view_y &&
           y < l->view_y + l->view_h;
}

/* Panning means looking elsewhere, which following would undo next frame. */
static void pan(App *a, int dx, int dy)
{
    inspect_stop(&a->inspect);
    camera_pan(&a->camera, dx, dy);
}

static void click(App *a, int x, int y)
{
    int sx = x - a->layout.view_x;
    int sy = y - a->layout.view_y;
    if (!camera_on_world(&a->camera, sx, sy)) {
        inspect_stop(&a->inspect);
        return;
    }
    float wx, wy;
    camera_to_world(&a->camera, sx, sy, &wx, &wy);
    scene_click(&a->inspect, &a->sim, wx, wy);
}

/* A click on the panel: follow who it names. While following, the panel lists that
 * creature instead, so these rows are not there to click. */
static void click_panel(App *a, int x, int y)
{
    if (a->inspect.following) {
        return;
    }
    int slot;
    switch (scene_panel_pick(&a->layout, x, y)) {
    case PANEL_PICK_TOP_HUNTER:
        slot = inspect_highest_level(&a->sim);
        break;
    case PANEL_PICK_OLDEST:
        slot = inspect_oldest(&a->sim);
        break;
    case PANEL_PICK_LARGEST_FAMILY:
        slot = inspect_largest_of_family(&a->sim,
                                         family_largest(&a->families, &a->sim));
        break;
    default:
        return;
    }
    if (slot >= 0) {
        inspect_follow(&a->inspect, &a->sim, slot);
    } else {
        snprintf(a->notice, sizeof(a->notice), "nobody to follow");
        a->notice_frames = NOTICE_FRAMES;
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
    inspect_stop(&a->inspect);
    family_reset(&a->families);
    a->ticks_due = 0;
}

static void handle_key(App *a, int key)
{
    switch (key) {
    case 'q':
        if (!PLATFORM_IS_PAGE) { /* stopping a page would only freeze it */
            a->running = false;
        }
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
    case 'n': /* only here, where it cannot end a living world by a slip */
        if (sim_is_over(&a->sim)) {
            new_world(a);
        }
        break;
    case 's':
        if (!PLATFORM_IS_PAGE) {
            app_save_now(&a->sim, a->opt->save_path, a->notice,
                         sizeof(a->notice));
            a->notice_frames = NOTICE_FRAMES;
        }
        break;
    case ']':
        camera_zoom(&a->camera, 1, a->camera.view_w / 2, a->camera.view_h / 2);
        break;
    case '[':
        camera_zoom(&a->camera, -1, a->camera.view_w / 2, a->camera.view_h / 2);
        break;
    case KEY_LEFT: pan(a, -ARROW_PAN, 0); break;
    case KEY_RIGHT: pan(a, ARROW_PAN, 0); break;
    case KEY_UP: pan(a, 0, -ARROW_PAN); break;
    case KEY_DOWN: pan(a, 0, ARROW_PAN); break;
    case KEY_ESCAPE: inspect_stop(&a->inspect); break;
    default: break;
    }
}

static void handle_event(App *a, const Event *e)
{
    a->redraw = true;
    switch (e->kind) {
    case EV_QUIT:
        a->running = false;
        break;
    case EV_KEY:
        handle_key(a, e->key);
        break;
    case EV_WHEEL:
        if (over_view(a, e->x, e->y)) {
            /* The camera counts from the view's own corner. */
            camera_zoom(&a->camera, e->delta, e->x - a->layout.view_x,
                        e->y - a->layout.view_y);
        }
        break;
    case EV_DOWN:
        a->pressed = over_view(a, e->x, e->y);
        if (!a->pressed) {
            click_panel(a, e->x, e->y);
        }
        a->dragging = false;
        a->press_x = a->mouse_x = e->x;
        a->press_y = a->mouse_y = e->y;
        break;
    case EV_UP:
        if (a->pressed && !a->dragging) {
            click(a, e->x, e->y);
        }
        a->pressed = false;
        a->dragging = false;
        break;
    case EV_MOVE:
        if (a->pressed && !a->dragging &&
            (abs(e->x - a->press_x) > CLICK_SLOP ||
             abs(e->y - a->press_y) > CLICK_SLOP)) {
            a->dragging = true;
        }
        if (a->dragging) {
            /* Dragging pulls the world along: the view moves the other way. */
            pan(a, a->mouse_x - e->x, a->mouse_y - e->y);
            a->mouse_x = e->x;
            a->mouse_y = e->y;
        }
        break;
    case EV_RESIZE:
        apply_window_size(a, e->w, e->h);
        break;
    }
}

static void draw_title(App *a, const Creature *followed)
{
    char year[32];
    panel_format_count(year, sizeof(year),
                       years_whole(a->sim.tick, a->sim.set.ticks_per_year));
    char who[32] = "";
    if (followed != NULL) {
        snprintf(who, sizeof(who), " | following #%u", (unsigned)followed->id);
    }
    char text[400];
    snprintf(text, sizeof(text),
             " agon | seed 0x%" PRIx64 " | year %s, %s | creatures %d | %d/s%s%s%s%s",
             a->sim.seed, year,
             years_season(a->sim.tick, a->sim.set.ticks_per_year), a->sim.pool.count, APP_SPEEDS[a->speed],
             sim_is_over(&a->sim) ? " | game over" : a->paused ? " | paused" : "",
             who,
             a->notice[0] != '\0' ? " | " : "", a->notice);
    cells_canvas_set(&a->canvas, 0, 0);
    render_text(0, 0, text, a->canvas.w / FONT_W + 1);
}

static void draw_help(App *a)
{
    cells_canvas_set(&a->canvas, 0, a->layout.help_y);
    /* Adjacent strings are one string, so each of these is one piece of text. */
    const char *help =
        PLATFORM_IS_PAGE
            ? " space pause | +/- speed | [ ] zoom | click follow | Esc let go"
              " | r new world"
            : " space pause | +/- speed | [ ] zoom | click follow | Esc let go"
              " | r new world | s save | q quit";
    render_text(0, 0, help, a->canvas.w / FONT_W + 1);
}

/* The enlarged body, with a caption above it. */
static void draw_enlarged(App *a, const Creature *followed, uint32_t lineage,
                          const FamilySample *newest)
{
    const Layout *l = &a->layout;
    char caption[32] = " no family yet";
    if (followed != NULL) {
        snprintf(caption, sizeof(caption), " creature #%u", (unsigned)followed->id);
    } else if (lineage != FAMILY_NONE) {
        snprintf(caption, sizeof(caption), " largest: #%u", (unsigned)lineage);
    }
    cells_canvas_set(&a->canvas, l->panel_x, l->family_y);
    render_text(0, 0, caption, PANEL_COLS);
    /* In the middle of the panel's width, below the caption. */
    int x = l->panel_x + (SCENE_PANEL_W - SCENE_BODY_BIG) / 2;
    int y = l->family_y + FONT_H;
    if (followed != NULL) {
        scene_draw_followed(&a->scene, &a->canvas, &a->sim, followed, a->body_ms,
                            x, y);
    } else if (newest != NULL) {
        scene_draw_sample(&a->canvas, newest, a->body_ms, x, y);
    }
}

/* The family's bodies over time, with a caption above them. A family outside the
 * recorded ones has no strip, and the caption says so. */
static void draw_strip(App *a, uint32_t lineage, int samples)
{
    const Layout *l = &a->layout;
    int y = l->family_y + (SCENE_ENLARGED_ROWS * FONT_H);
    cells_canvas_set(&a->canvas, l->panel_x, y);
    char caption[32];
    /* The longest id, 10 digits, would still only lose the end of the words. */
    snprintf(caption, sizeof(caption), samples == 0 ? " #%u: no record" : " family #%u",
             (unsigned)lineage);
    render_text(0, 0, lineage != FAMILY_NONE ? caption : "", PANEL_COLS);
    int w = FAMILY_STRIP * 16;
    scene_draw_strip(&a->canvas, &a->families, lineage,
                     l->panel_x + (SCENE_PANEL_W - w) / 2, y + FONT_H, w);
}

/* The bottom of the panel: the followed creature and its family, or else the
 * largest family and its newest body. */
static void draw_family(App *a, const Creature *followed)
{
    if (!a->layout.show_enlarged) {
        return;
    }
    uint32_t lineage = followed != NULL ? followed->lineage
                                        : family_largest(&a->families, &a->sim);
    FamilySample samples[FAMILY_STRIP];
    int n = family_strip(&a->families, lineage, samples);
    draw_enlarged(a, followed, lineage, n > 0 ? &samples[n - 1] : NULL);
    if (a->layout.show_strip) {
        draw_strip(a, lineage, n);
    }
}

static void draw_panel(App *a, const Creature *followed)
{
    const Layout *l = &a->layout;
    if (!l->has_panel) {
        return;
    }
    /* Rows are whole; this clears what is left below the last of them. */
    canvas_rect(&a->canvas, l->panel_x, l->panel_y, SCENE_PANEL_W, l->view_h,
                canvas_rgb(PALETTE_TEXT_BACKGROUND.r, PALETTE_TEXT_BACKGROUND.g,
                           PALETTE_TEXT_BACKGROUND.b));
    cells_canvas_set(&a->canvas, l->panel_x, l->panel_y);
    if (followed != NULL) {
        panel_draw_inspect(&a->sim, &a->inspect, 0, 0, l->panel_rows);
    } else {
        panel_draw(&a->sim, 0, 0, l->panel_rows);
    }
    draw_family(a, followed);
}

/* Too small for anything else: a minimised window lands here too. */
static void draw_too_small(App *a)
{
    canvas_fill(&a->canvas, canvas_rgb(0, 0, 0));
    cells_canvas_set(&a->canvas, 0, 0);
    render_too_small(a->canvas.w / FONT_W, a->canvas.h / FONT_H, SCENE_MIN_COLS,
                     SCENE_MIN_ROWS);
}

/* The creature being followed, moving the camera; a death here tells the user
 * and drops back to the ordinary panel. */
static const Creature *update_following(App *a, float fraction)
{
    bool was_following = a->inspect.following;
    const Creature *followed =
        scene_follow(&a->scene, &a->camera, &a->inspect, &a->sim, fraction);
    if (was_following && followed == NULL) {
        inspect_stop(&a->inspect);
        snprintf(a->notice, sizeof(a->notice), "the creature you followed died");
        a->notice_frames = NOTICE_FRAMES;
    }
    return followed;
}

static void draw_frame(App *a, float fraction)
{
    const Creature *followed = update_following(a, fraction);
    if (a->layout.too_small) {
        draw_too_small(a);
        return;
    }
    const Layout *l = &a->layout;
    scene_draw_world(&a->scene, &a->canvas, &a->camera, &a->sim, fraction,
                     a->last_ms, a->body_ms, l->view_x, l->view_y);
    scene_draw_cursor(&a->scene, &a->canvas, &a->camera, &a->sim, followed,
                      fraction, l->view_x, l->view_y);
    if (sim_is_over(&a->sim)) {
        scene_draw_game_over(&a->canvas, &a->sim,
                             PLATFORM_IS_PAGE ? "n new world"
                                              : "n new world | q quit",
                             l->view_x, l->view_y, l->view_w, l->view_h);
    }
    /* After the world: a body near the edge may have been drawn over these. */
    draw_title(a, followed);
    draw_help(a);
    draw_panel(a, followed);
}

static void advance_clocks(App *a)
{
    uint64_t now = platform_now_ms();
    if (!a->paused && !sim_is_over(&a->sim)) {
        a->body_ms += now - a->last_ms;
    }
    a->last_ms = now;
    if (a->notice_frames > 0 && --a->notice_frames == 0) {
        a->notice[0] = '\0';
        a->redraw = true;
    }
}

/*
 * Whether this frame looks different from the last. Bodies stop moving while
 * paused and only the water goes on, so a paused window draws about four times
 * a second instead of thirty. Events are still read every frame.
 */
static bool frame_needs_drawing(App *a)
{
    int water = scene_water_frame(a->last_ms);
    /* A dead world is as still as a paused one, so it costs as little. */
    bool still = a->paused || sim_is_over(&a->sim);
    bool needed = !still || a->redraw || water != a->last_water;
    if (needed) {
        a->last_water = water;
        a->redraw = false;
    }
    return needed;
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
    if (!a->paused && !sim_is_over(&a->sim)) {
        a->ticks_due += APP_SPEEDS[a->speed];
        /* Checked on every tick: at high speed a frame runs several, and the
         * world may end during the first. */
        while (a->ticks_due >= FRAMES_PER_SECOND && !sim_is_over(&a->sim)) {
            sim_step(&a->sim);
            a->ticks_due -= FRAMES_PER_SECOND;
        }
        if (sim_is_over(&a->sim)) {
            a->ticks_due = 0;  /* nothing is on its way any more */
            a->redraw = true;  /* the frame that shows the screen */
        }
    }
    scene_update(&a->scene, &a->sim, APP_SPEEDS[a->speed]);
    family_update(&a->families, &a->sim);
    advance_clocks(a);

    if (frame_needs_drawing(a)) {
        float fraction = (float)a->ticks_due / (float)FRAMES_PER_SECOND;
        draw_frame(a, fraction);
        platform_present(&a->canvas);
    }

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
    /* static, like app below: in a browser page main never finishes and its
     * stack is reused, while frames keep reading the options through app.opt.
     * A plain local here would be a pointer to memory that changes under us. */
    static Options opt;
    char err[512];
    if (!options_parse(argc, argv, &opt, err, sizeof(err))) {
        char message[600];
        snprintf(message, sizeof(message), "%s (see agon-window --help)", err);
        platform_error(message);
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
        platform_error("--once and --settings are for agon");
        return EXIT_USAGE;
    }
    if (PLATFORM_IS_PAGE && (opt.save_path != NULL || opt.load_path != NULL)) {
        platform_error("a browser page has nowhere to save or load from");
        return EXIT_USAGE;
    }
    int zoom = CAMERA_NORMAL;
    if (opt.zoom_name != NULL && !zoom_from_name(opt.zoom_name, &zoom)) {
        platform_error("--zoom needs close, normal, far or world");
        return EXIT_USAGE;
    }
    Settings settings;
    if (!read_settings(&opt, &settings, err, sizeof(err))) {
        platform_error(err);
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
    app.redraw = true;

    if (!scene_init(&app.scene, &app.sim) ||
        !family_init(&app.families, app.sim.pool.capacity) ||
        !canvas_init(&app.canvas, WINDOW_W, WINDOW_H)) {
        platform_error("out of memory");
        return EXIT_FAILED;
    }
    scene_layout(&app.layout, WINDOW_W, WINDOW_H);
    camera_init(&app.camera, app.sim.world.w, app.sim.world.h,
                app.layout.view_w, app.layout.view_h);
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
    family_free(&app.families);
    scene_free(&app.scene);
    sim_free(&app.sim);
    return result;
}
