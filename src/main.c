#include <inttypes.h> /* PRIx64, PRIu64: printf formats for uint64_t */
#include <stdint.h>
#include <stdio.h>
#include <time.h>   /* clock_gettime, nanosleep, struct timespec (POSIX) */

#include "app.h"
#include "inspect.h"
#include "options.h"
#include "panel.h"
#include "cells.h"
#include "render.h"
#include "rng.h"
#include "save.h"
#include "settings.h"
#include "sim.h"
#include "term.h"

#define FRAMES_PER_SECOND 30
#define NANOSECONDS_PER_SECOND 1000000000L
#define FRAME_NANOSECONDS (NANOSECONDS_PER_SECOND / FRAMES_PER_SECOND)

/* Smallest window we draw in; below it, a message instead. */
#define MIN_COLS 20
#define MIN_ROWS 6

/* How long a message stays on the title line: two seconds. */
#define NOTICE_FRAMES (2 * FRAMES_PER_SECOND)

/* --once with no --size: a world that prints as 80 columns by 20 lines. */
#define ONCE_WIDTH 80
#define ONCE_HEIGHT 40

/* A seed for when none is given: the current time, mixed. */
static uint64_t seed_from_clock(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    uint64_t state = (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
    return rng_splitmix64(&state);
}

/* Nanoseconds from a fixed but arbitrary starting point. CLOCK_MONOTONIC
 * never jumps back when the system clock is changed. */
static int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * NANOSECONDS_PER_SECOND + t.tv_nsec;
}

static void sleep_until(int64_t deadline)
{
    int64_t left = deadline - now_ns();
    if (left <= 0) {
        return;
    }
    struct timespec pause = {
        .tv_sec = (time_t)(left / NANOSECONDS_PER_SECOND),
        .tv_nsec = (long)(left % NANOSECONDS_PER_SECOND),
    };
    /* A signal (a window resize) can end the sleep early. That only
     * makes one frame come a little sooner, so it is not retried. */
    nanosleep(&pause, NULL);
}

static void draw_title(int cols, const Sim *sim, int speed, int paused,
                       bool inspecting, const char *notice)
{
    char tick[32];
    /* tick is a uint64_t; every value a run reaches fits in an int64_t. */
    panel_format_count(tick, sizeof(tick), (int64_t)sim->tick);
    char text[320];
    /* PRIx64 expands to the right printf letters for a uint64_t on this
     * platform ("lx" here); writing %llx would be wrong on some systems.
     * Adjacent string literals are joined into one by the compiler. */
    snprintf(text, sizeof(text),
             " agon | seed 0x%" PRIx64 " | tick %s | creatures %d | %d/s%s%s%s%s",
             sim->seed, tick, sim->pool.count, APP_SPEEDS[speed],
             paused ? " | paused" : "", inspecting ? " | inspect" : "",
             notice[0] != '\0' ? " | " : "", notice);
    render_text(0, 0, text, cols);
}

static void draw_help(int cols, int rows, bool inspecting)
{
    const char *help =
        inspecting
            ? " arrows/hjkl move | HJKL by 5 | i or Esc leave | space pause"
              " | q quit"
            : " space pause | +/- speed | r new seed | i inspect | s save | q quit";
    render_text(0, rows - 1, help, cols);
}

/* The keys that move the cursor in inspect mode, as a step in cells. */
enum { KEY_UP = 1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ESCAPE };

/*
 * The next key, with the arrow keys turned into KEY_ values. An arrow key
 * arrives as three bytes: the escape character (27), '[' and a letter, A
 * to D. The Esc key alone sends just the 27. In raw mode all three bytes
 * of an arrow are normally waiting together, so a 27 with nothing after
 * it is taken to be Esc. -1 when no key is waiting.
 */
static int read_key(void)
{
    int key = term_key();
    if (key != 27) {
        return key;
    }
    int second = term_key();
    if (second == -1) {
        return KEY_ESCAPE;
    }
    if (second != '[') {
        return -1; /* Alt plus a key, or something else: ignored */
    }
    switch (term_key()) {
    case 'A': return KEY_UP;
    case 'B': return KEY_DOWN;
    case 'C': return KEY_RIGHT;
    case 'D': return KEY_LEFT;
    default: return -1; /* another escape sequence (F1, Home, ...) */
    }
}

/* The step (dx, dy) a key moves the cursor by; false for other keys. */
static bool cursor_step(int key, int *dx, int *dy)
{
    *dx = 0;
    *dy = 0;
    switch (key) {
    case KEY_LEFT: case 'h': *dx = -1; break;
    case KEY_RIGHT: case 'l': *dx = 1; break;
    case KEY_UP: case 'k': *dy = -1; break;
    case KEY_DOWN: case 'j': *dy = 1; break;
    case 'H': *dx = -5; break;
    case 'L': *dx = 5; break;
    case 'K': *dy = -5; break;
    case 'J': *dy = 5; break;
    default: return false;
    }
    return true;
}

/* How much of the world is on screen: at most the world, at most the
 * area beside the panel, two cells per character row. */
static void visible_cells(const Sim *sim, int world_cols, int rows,
                          int *max_x, int *max_y)
{
    *max_x = world_cols < sim->world.w ? world_cols : sim->world.w;
    int cell_rows = 2 * (rows - 2);
    *max_y = cell_rows < sim->world.h ? cell_rows : sim->world.h;
}

/* The columns the world gets: all of them, or all but the panel's when
 * the window is wide enough for both. */
static int world_columns(int cols)
{
    return cols >= MIN_COLS + PANEL_COLS ? cols - PANEL_COLS : cols;
}

/* The three lines --once prints under the world. */
static void print_summary(const Sim *sim)
{
    const Stats *st = &sim->stats;
    char tick[32], plants[32], oldest[32];
    panel_format_count(tick, sizeof(tick), (int64_t)sim->tick);
    panel_format_count(plants, sizeof(plants), st->planted);
    panel_format_count(oldest, sizeof(oldest), (int64_t)st->oldest_age);
    printf("agon | seed 0x%" PRIx64 " | tick %s | world %d x %d\n", sim->seed,
           tick, sim->world.w, sim->world.h);
    printf("creatures %d: %d eat plants, %d eat meat (%d top hunters) | "
           "%d families\n",
           st->creatures, st->by_level[BAND_PLANT],
           st->by_level[BAND_MEAT] + st->by_level[BAND_TOP],
           st->by_level[BAND_TOP], st->lineages);
    printf("plants on %s cells | carrion on %d cells | oldest %s ticks\n",
           plants, st->carrion_cells, oldest);
}

/* agon --once: build the world, run it, print it and a summary. Never
 * opens the terminal, so it works in a pipe, a file or a shell start-up
 * script. */
static int run_once(const Options *o, const Settings *settings,
                    uint64_t seed)
{
    int w = o->size_given ? o->width : ONCE_WIDTH;
    int h = o->size_given ? o->height : ONCE_HEIGHT;
    Sim sim;
    char err[512];
    if (!app_open_world(&sim, o, settings, seed, w, h, err, sizeof(err))) {
        fprintf(stderr, "agon: %s\n", err);
        return EXIT_FAILED;
    }
    for (long i = 0; i < o->ticks; i++) {
        sim_step(&sim);
    }
    /* Before the fresh sample below, which would add a point to the
     * histories that the running world never took. */
    if (o->save_path != NULL &&
        !save_write(&sim, o->save_path, err, sizeof(err))) {
        fprintf(stderr, "agon: %s\n", err);
        sim_free(&sim);
        return EXIT_FAILED;
    }
    /* Before the fresh sample below, like the save. */
    uint64_t checksum = sim_checksum(&sim);
    /* A fresh sample: the last one may be up to stats_every ticks old. */
    stats_sample(&sim.stats, &sim.world, &sim.pool, sim.tick, sim.scratch);
    term_detect_colour();
    render_snapshot(&sim);
    print_summary(&sim);
    if (o->checksum) {
        printf("checksum 0x%016" PRIx64 "\n", checksum);
    }
    sim_free(&sim);
    return EXIT_FINE;
}

int main(int argc, char **argv)
{
    Options opt;
    char err[512]; /* room for a long path in a message */
    /* Bad options are reported before the screen is taken over, so the
     * message stays visible. */
    if (!options_parse(argc, argv, &opt, err, sizeof(err))) {
        fprintf(stderr, "agon: %s (see agon --help)\n", err);
        return EXIT_USAGE;
    }
    if (opt.help) {
        fputs(OPTIONS_USAGE, stdout);
        return EXIT_FINE;
    }
    if (opt.version) {
        printf("agon %s\n", AGON_VERSION);
        return EXIT_FINE;
    }
    uint64_t seed = opt.seed_given ? opt.seed : seed_from_clock();
    if (opt.list_settings) {
        Settings defaults = settings_default();
        settings_print(stdout, &defaults);
        return EXIT_FINE;
    }
    Settings settings = settings_default();
    /* The file first, then the options: the command line has the last
     * word, as with most Unix tools. */
    if (opt.config_path != NULL &&
        !settings_load(&settings, opt.config_path, err, sizeof(err))) {
        fprintf(stderr, "agon: %s\n", err);
        return EXIT_USAGE;
    }
    if (opt.no_floor) {
        settings.min_creatures = 0;
    }
    /* These only mean something with a window to draw in. */
    if (opt.frames > 0 || opt.screenshot_path != NULL || opt.zoom_name != NULL) {
        fprintf(stderr, "agon: --frames, --screenshot and --zoom are for "
                        "agon-window\n");
        return EXIT_USAGE;
    }
    if (opt.once) {
        return run_once(&opt, &settings, seed);
    }

    if (!term_open()) {
        fprintf(stderr, "agon: needs a terminal (do not redirect input or "
                        "output; agon --once works without one)\n");
        return EXIT_FAILED;
    }

    int cols, rows;
    term_size(&cols, &rows);

    /* The world is sized once, from the window at start-up (never smaller
     * than the minimum window), and keeps that size: resizing changes how
     * much of it is visible. Two cells per character row; the top row is
     * the title line and the bottom row the help line. */
    int world_w = world_columns(cols < MIN_COLS ? MIN_COLS : cols);
    int world_h = 2 * ((rows < MIN_ROWS ? MIN_ROWS : rows) - 2);
    if (opt.size_given) {
        world_w = opt.width;
        world_h = opt.height;
    }

    /* The Sim struct lives on main's stack; the world's layers it points
     * to live on the heap until sim_free. */
    Sim sim;
    if (!app_open_world(&sim, &opt, &settings, seed, world_w, world_h, err,
                    sizeof(err))) {
        term_close(); /* so the message lands on the normal screen */
        fprintf(stderr, "agon: %s\n", err);
        return EXIT_FAILED;
    }

    /* New seeds for r come from their own generator, so pressing r gives
     * the same series of worlds after the same starting seed. */
    Rng seed_source;
    rng_seed(&seed_source, sim.seed); /* a loaded world has its own seed */

    int speed = opt.speed > 0 ? app_speed_step(opt.speed) : APP_DEFAULT_SPEED;
    int paused = 0;
    int running = 1;
    int need_clear = 1;
    int failed = 0;
    /* Ticks owed, in thirtieths: each frame adds the speed, and every 30
     * owed pays for one tick. Keeps whole numbers only. */
    int ticks_due = 0;
    int64_t next_frame = now_ns();
    Inspect inspect = {0}; /* off until i is pressed */
    /* A message for the title line, cleared after NOTICE_FRAMES frames. */
    char notice[256] = "";
    int notice_frames = 0;

    while (running) {
        if (term_resized()) {
            term_size(&cols, &rows);
            need_clear = 1;
        }

        int max_x, max_y;
        visible_cells(&sim, world_columns(cols), rows, &max_x, &max_y);
        int key;
        while ((key = read_key()) != -1) {
            int dx, dy;
            if (key == 'q') {
                running = 0;
            } else if (key == ' ') {
                paused = !paused;
            } else if (key == 'i') {
                if (inspect.on) {
                    inspect.on = false;
                } else {
                    inspect_start(&inspect, &sim, max_x, max_y);
                }
            } else if (key == KEY_ESCAPE) {
                inspect.on = false;
            } else if (inspect.on && cursor_step(key, &dx, &dy)) {
                inspect_move(&inspect, &sim, dx, dy, max_x, max_y);
            } else if ((key == '+' || key == '=') && speed < APP_SPEED_COUNT - 1) {
                speed++;
            } else if (key == '-' && speed > 0) {
                speed--;
            } else if (key == 's') {
                app_save_now(&sim, opt.save_path, notice, sizeof(notice));
                notice_frames = NOTICE_FRAMES;
            } else if (key == 'r') {
                if (!sim_reseed(&sim, rng_next(&seed_source))) {
                    failed = 1; /* the brain could not be reopened */
                    running = 0;
                } else if (inspect.on) {
                    /* Ids start again from 0 in a new world: the id being
                     * followed now belongs to someone else. */
                    inspect_start(&inspect, &sim, max_x, max_y);
                }
            }
        }

        if (!paused) {
            ticks_due += APP_SPEEDS[speed];
            while (ticks_due >= FRAMES_PER_SECOND) {
                sim_step(&sim);
                ticks_due -= FRAMES_PER_SECOND;
            }
        }
        if (inspect.on) {
            inspect_update(&inspect, &sim);
        }

        term_begin();
        if (need_clear) {
            term_clear();
            render_invalidate(); /* the screen is blank: redraw it all */
            need_clear = 0;
        }
        if (cols < MIN_COLS || rows < MIN_ROWS) {
            render_too_small(cols, rows, MIN_COLS, MIN_ROWS);
        } else {
            draw_title(cols, &sim, speed, paused, inspect.on, notice);
            /* The panel only when the world keeps its minimum width
             * beside it: on a narrow window the panel goes first. */
            int world_cols = world_columns(cols);
            if (world_cols < cols && inspect.on) {
                panel_draw_inspect(&sim, &inspect, world_cols, 1, rows - 2);
            } else if (world_cols < cols) {
                panel_draw(&sim, world_cols, 1, rows - 2);
            }
            render_world(&sim, 0, 1, world_cols, rows - 2);
            if (inspect.on) {
                render_cursor(&sim, 0, 1, world_cols, rows - 2, inspect.x,
                              inspect.y);
            }
            draw_help(cols, rows, inspect.on);
        }
        term_end();

        if (notice_frames > 0) {
            notice_frames--;
            if (notice_frames == 0) {
                notice[0] = '\0';
            }
        }

        next_frame += FRAME_NANOSECONDS;
        int64_t now = now_ns();
        if (now > next_frame + FRAME_NANOSECONDS) {
            next_frame = now; /* fell far behind: do not try to catch up */
        }
        sleep_until(next_frame);
    }

    term_close();
    if (failed) {
        sim_free(&sim);
        fprintf(stderr, "agon: out of memory while building a new world\n");
        return EXIT_FAILED;
    }
    /* On quit, after term_close, so a failure is seen on the normal screen
     * and the exit code says so. Only q comes here: Ctrl-C and other
     * signals end the program from term.c's handler, where writing a file
     * is not safe, so they do not save. */
    if (opt.save_path != NULL &&
        !save_write(&sim, opt.save_path, err, sizeof(err))) {
        sim_free(&sim);
        fprintf(stderr, "agon: %s\n", err);
        return EXIT_FAILED;
    }
    if (opt.checksum) {
        printf("checksum 0x%016" PRIx64 "\n", sim_checksum(&sim));
    }
    sim_free(&sim);
    return EXIT_FINE;
}
