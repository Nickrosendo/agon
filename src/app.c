#include "app.h"

#include <stdio.h> /* snprintf */

#include "save.h"

const int APP_SPEEDS[] = {1, 2, 5, 10, 15, 30, 60, 120};

/* Stops the build if someone adds a speed and forgets APP_SPEED_COUNT.
 * sizeof works here because this file has the array's definition. */
_Static_assert(sizeof(APP_SPEEDS) / sizeof(APP_SPEEDS[0]) == APP_SPEED_COUNT,
               "APP_SPEED_COUNT must match APP_SPEEDS");

int app_speed_step(int ticks_per_second)
{
    int step = 0;
    for (int i = 0; i < APP_SPEED_COUNT; i++) {
        if (APP_SPEEDS[i] <= ticks_per_second) {
            step = i;
        }
    }
    return step;
}

bool app_open_world(Sim *sim, const Options *o, const Settings *settings,
                    uint64_t seed, int w, int h, char *err, size_t errlen)
{
    if (o->load_path != NULL) {
        return save_read(sim, o->load_path, err, errlen);
    }
    if (!sim_init(sim, settings, seed, w, h)) {
        snprintf(err, errlen, "could not start: out of memory, or no brain "
                              "called '%s'",
                 settings->brain_name);
        return false;
    }
    return true;
}

void app_save_now(const Sim *sim, const char *path, char *notice, size_t size)
{
    if (path == NULL) {
        snprintf(notice, size, "nowhere to save: start with --save PATH");
    } else if (save_write(sim, path, notice, size)) {
        snprintf(notice, size, "saved to %s", path);
    }
    /* On failure save_write has already put its message in notice. */
}
