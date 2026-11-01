#include <errno.h>
#include <inttypes.h> /* PRIx64, PRIu64: printf formats for uint64_t */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h> /* strtoull */
#include <string.h> /* strcmp */
#include <time.h>   /* clock_gettime, nanosleep, struct timespec (POSIX) */

#include "render.h"
#include "rng.h"
#include "settings.h"
#include "sim.h"
#include "term.h"

#define FRAMES_PER_SECOND 30
#define NANOSECONDS_PER_SECOND 1000000000L
#define FRAME_NANOSECONDS (NANOSECONDS_PER_SECOND / FRAMES_PER_SECOND)

/* Smallest window we draw in; below it, a message instead. */
#define MIN_COLS 20
#define MIN_ROWS 6

/* Ticks per second for each speed step; + and - move along it. */
static const int SPEEDS[] = {1, 2, 5, 10, 15, 30, 60, 120};
#define SPEED_COUNT ((int)(sizeof(SPEEDS) / sizeof(SPEEDS[0])))
#define DEFAULT_SPEED 3 /* 10 ticks per second */

/* A seed for when none is given: the current time, mixed. */
static uint64_t seed_from_clock(void)
{
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    uint64_t state = (uint64_t)t.tv_sec * 1000000000ULL + (uint64_t)t.tv_nsec;
    return rng_splitmix64(&state);
}

/*
 * Parses "123" or "0x7b" into *seed. strtoull alone is too forgiving: it
 * skips leading spaces, accepts "-1" (and turns it into 2^64 - 1), stops
 * quietly at the first bad character, and reports a too-large number only
 * through errno. Each of those is checked here.
 */
static int parse_seed(const char *text, uint64_t *seed)
{
    if (text[0] < '0' || text[0] > '9') {
        return 0; /* empty, a sign, or a space */
    }
    char *end;
    errno = 0;
    /* Base 0: decimal, or hexadecimal after 0x. end is set to the first
     * character not used. */
    unsigned long long value = strtoull(text, &end, 0);
    if (errno == ERANGE || *end != '\0') {
        return 0;
    }
    *seed = value;
    return 1;
}

static int parse_args(int argc, char **argv, uint64_t *seed)
{
    /* argv[0] is the program's own name; options start at argv[1]. */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "agon: --seed needs a number after it\n");
                return 0;
            }
            i++;
            if (!parse_seed(argv[i], seed)) {
                fprintf(stderr, "agon: '%s' is not a seed (a whole number, "
                                "decimal or 0x hexadecimal)\n",
                        argv[i]);
                return 0;
            }
        } else {
            fprintf(stderr, "agon: unknown option '%s'\n"
                            "usage: agon [--seed N]\n",
                    argv[i]);
            return 0;
        }
    }
    return 1;
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

static void draw_title(int cols, const Sim *sim, int speed, int paused)
{
    char text[160];
    /* PRIx64 expands to the right printf letters for a uint64_t on this
     * platform ("lx" here); writing %llx would be wrong on some systems.
     * Adjacent string literals are joined into one by the compiler. */
    snprintf(text, sizeof(text),
             " agon | seed 0x%" PRIx64 " | tick %" PRIu64
             " | creatures %d | %d/s%s",
             sim->seed, sim->tick, sim->pool.count, SPEEDS[speed],
             paused ? " paused" : "");
    render_text(0, 0, text, cols);
}

int main(int argc, char **argv)
{
    uint64_t seed = seed_from_clock();
    /* Bad options are reported before the screen is taken over, so the
     * message stays visible. */
    if (!parse_args(argc, argv, &seed)) {
        return 1;
    }
    Settings settings = settings_default();

    if (!term_open()) {
        fprintf(stderr, "agon: needs a terminal (do not redirect input or "
                        "output)\n");
        return 1;
    }

    int cols, rows;
    term_size(&cols, &rows);

    /* The world is sized once, from the window at start-up (never smaller
     * than the minimum window), and keeps that size: resizing changes how
     * much of it is visible. Two cells per character row; the top row is
     * the title line and the bottom row the help line. */
    int world_w = cols < MIN_COLS ? MIN_COLS : cols;
    int world_h = 2 * ((rows < MIN_ROWS ? MIN_ROWS : rows) - 2);

    /* The Sim struct lives on main's stack; the world's layers it points
     * to live on the heap until sim_free. */
    Sim sim;
    if (!sim_init(&sim, &settings, seed, world_w, world_h)) {
        term_close(); /* so the message lands on the normal screen */
        fprintf(stderr, "agon: could not start: out of memory, or no "
                        "brain called '%s'\n",
                settings.brain_name);
        return 1;
    }

    /* New seeds for r come from their own generator, so pressing r gives
     * the same series of worlds after the same starting seed. */
    Rng seed_source;
    rng_seed(&seed_source, seed);

    int speed = DEFAULT_SPEED;
    int paused = 0;
    int running = 1;
    int need_clear = 1;
    int failed = 0;
    /* Ticks owed, in thirtieths: each frame adds the speed, and every 30
     * owed pays for one tick. Keeps whole numbers only. */
    int ticks_due = 0;
    int64_t next_frame = now_ns();

    while (running) {
        if (term_resized()) {
            term_size(&cols, &rows);
            need_clear = 1;
        }

        int key;
        while ((key = term_key()) != -1) {
            if (key == 'q') {
                running = 0;
            } else if (key == ' ') {
                paused = !paused;
            } else if ((key == '+' || key == '=') && speed < SPEED_COUNT - 1) {
                speed++;
            } else if (key == '-' && speed > 0) {
                speed--;
            } else if (key == 'r') {
                if (!sim_reseed(&sim, rng_next(&seed_source))) {
                    failed = 1; /* the brain could not be reopened */
                    running = 0;
                }
            }
        }

        if (!paused) {
            ticks_due += SPEEDS[speed];
            while (ticks_due >= FRAMES_PER_SECOND) {
                sim_step(&sim);
                ticks_due -= FRAMES_PER_SECOND;
            }
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
            draw_title(cols, &sim, speed, paused);
            render_world(&sim, 0, 1, cols, rows - 2);
            render_text(0, rows - 1,
                        " space pause | +/- speed | r new seed | q quit",
                        cols);
        }
        term_end();

        next_frame += FRAME_NANOSECONDS;
        int64_t now = now_ns();
        if (now > next_frame + FRAME_NANOSECONDS) {
            next_frame = now; /* fell far behind: do not try to catch up */
        }
        sleep_until(next_frame);
    }

    term_close();
    sim_free(&sim);
    if (failed) {
        fprintf(stderr, "agon: out of memory while building a new world\n");
        return 1;
    }
    return 0;
}
