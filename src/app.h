/*
 * app: what both programs do the same way around the simulation: the
 * speed steps, opening a world, the s key.
 */
#ifndef AGON_APP_H
#define AGON_APP_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "options.h"
#include "settings.h"
#include "sim.h"

#define AGON_VERSION "0.2.0"

/* What main returns to the shell: 0 for success, 1 when something failed
 * while running, 2 for a mistake on the command line (the convention of
 * many Unix tools, so scripts can tell the two apart). */
enum { EXIT_FINE = 0, EXIT_FAILED = 1, EXIT_USAGE = 2 };

/*
 * Ticks per second for each speed step; + and - move along it. The length
 * is a separate constant because a file including this header sees only
 * "an array exists": sizeof needs the definition, which is in app.c, and
 * app.c checks at compile time that the two agree.
 */
extern const int APP_SPEEDS[];
enum { APP_SPEED_COUNT = 8 };
#define APP_DEFAULT_SPEED 1 /* 2 ticks per second: slow enough to follow one creature */

/* The speed step for a number of ticks per second: the fastest step not
 * above it, or the slowest step when it is below them all. */
int app_speed_step(int ticks_per_second);

/* Builds the world the options ask for: from --load, or new. False with a
 * message in err; there is nothing to free then. */
bool app_open_world(Sim *sim, const Options *o, const Settings *settings,
                    uint64_t seed, int w, int h, char *err, size_t errlen);

/* The text of the game over screen, the same in all three programs. Each line
 * has room for the largest numbers a record can hold, and the compiler checks
 * that: with less, -Wformat-truncation reports the snprintf that could be cut. */
enum { APP_OVER_LINES = 6, APP_OVER_COLS = 64 };

/*
 * Fills the lines about a world that has ended and returns how many. The
 * parameter is written as an array of arrays: each row is APP_OVER_COLS chars,
 * and the compiler needs that number to find where lines[1] starts. Only the
 * first dimension can be left blank.
 */
/*
 * The prelude: a new world's first years, run as fast as the machine allows
 * before it is shown, so it opens on creatures that have evolved for a while
 * (the tactics design, section 9). until is the tick it ends at; active while
 * it runs. The world is the same as if it had been watched from the start.
 */
typedef struct {
    bool active;
    int years;
    uint64_t until;
} Prelude;

/* How long of each 33 ms frame the prelude may run, in milliseconds: most of
 * it, leaving enough to draw the world and read the keys. */
#define APP_PRELUDE_FRAME_MS 28

/* The years a world opened with these options gets: none for a loaded world,
 * --ancestors when given, else the setting. interactive is false for --once and
 * --frames, which get one only when --ancestors asks for it. */
int app_prelude_years(const Options *o, const Settings *s, bool interactive);
/* Starts a prelude of years for sim, which must be at tick 0 for it to run. */
void app_prelude_start(Prelude *p, const Sim *sim, int years);
/* Runs prelude ticks until budget_ms milliseconds by now_ms have passed, the
 * prelude is over or the world has died. Returns whether it is still on.
 * now_ms is a function, because each program reads its clock its own way. */
bool app_prelude_step(Prelude *p, Sim *sim, uint64_t (*now_ms)(void),
                      uint64_t budget_ms);
/* Runs the whole prelude at once: for runs that must come out the same every
 * time, which a clock's budget would not. */
void app_prelude_finish(Prelude *p, Sim *sim);
/* Ends the prelude where it got to: a key was pressed. */
void app_prelude_stop(Prelude *p);
/* "evolving ancestors: year 12 of 30, any key to stop" */
void app_prelude_text(const Prelude *p, const Sim *sim, char *out, size_t size);

int app_game_over_lines(const Sim *sim, char lines[][APP_OVER_COLS]);

/* The s key. The result goes to notice, for the title line: a failure must
 * not end a world that may have been running for days. */
void app_save_now(const Sim *sim, const char *path, char *notice, size_t size);

#endif
