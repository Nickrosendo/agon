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

#define AGON_VERSION "0.1.0"

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
#define APP_DEFAULT_SPEED 3 /* 10 ticks per second */

/* The speed step for a number of ticks per second: the fastest step not
 * above it, or the slowest step when it is below them all. */
int app_speed_step(int ticks_per_second);

/* Builds the world the options ask for: from --load, or new. False with a
 * message in err; there is nothing to free then. */
bool app_open_world(Sim *sim, const Options *o, const Settings *settings,
                    uint64_t seed, int w, int h, char *err, size_t errlen);

/* The s key. The result goes to notice, for the title line: a failure must
 * not end a world that may have been running for days. */
void app_save_now(const Sim *sim, const char *path, char *notice, size_t size);

#endif
