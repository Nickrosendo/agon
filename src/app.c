#include "app.h"

#include <inttypes.h> /* PRIx64 */
#include <stdio.h>    /* snprintf */

#include "panel.h" /* panel_format_count */
#include "save.h"
#include "years.h"

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

int app_prelude_years(const Options *o, const Settings *s, bool interactive)
{
    if (o->load_path != NULL) {
        return 0; /* a saved world has lived already */
    }
    if (o->ancestors >= 0) {
        return o->ancestors;
    }
    return interactive ? s->ancestor_years : 0;
}

void app_prelude_start(Prelude *p, const Sim *sim, int years)
{
    p->years = years > 0 ? years : 0;
    /* Converted before multiplying: years times a long year could pass the
     * largest int, and signed overflow is undefined behaviour. */
    p->until = (uint64_t)p->years * (uint64_t)sim->set.ticks_per_year;
    p->active = sim->tick < p->until && !sim_is_over(sim);
}

/* One tick of the prelude, ending it when it is over. */
static void prelude_tick(Prelude *p, Sim *sim)
{
    sim_step(sim);
    if (sim->tick >= p->until || sim_is_over(sim)) {
        p->active = false;
    }
}

bool app_prelude_step(Prelude *p, Sim *sim, uint64_t (*now_ms)(void),
                      uint64_t budget_ms)
{
    uint64_t start = now_ms();
    while (p->active) {
        prelude_tick(p, sim);
        /* Unsigned subtraction: right even if the clock wrapped. */
        if (now_ms() - start >= budget_ms) {
            break;
        }
    }
    return p->active;
}

void app_prelude_finish(Prelude *p, Sim *sim)
{
    while (p->active) {
        prelude_tick(p, sim);
    }
}

void app_prelude_stop(Prelude *p)
{
    p->active = false;
}

void app_prelude_text(const Prelude *p, const Sim *sim, char *out, size_t size)
{
    int year = years_whole(sim->tick, sim->set.ticks_per_year) + 1;
    snprintf(out, size, "evolving ancestors: year %d of %d, any key to stop",
             year < p->years ? year : p->years, p->years);
}

int app_game_over_lines(const Sim *sim, char lines[][APP_OVER_COLS])
{
    const WorldRecords *r = &sim->records;
    int per_year = sim->set.ticks_per_year;
    char when[32], peak[32], age[32];
    panel_format_count(when, sizeof(when), years_whole(r->ended_at, per_year));
    panel_format_count(peak, sizeof(peak), r->peak_population);
    years_format(age, sizeof(age), r->oldest_age, per_year);
    int n = 0;
    snprintf(lines[n++], APP_OVER_COLS, "game over");
    if (r->peak_population == 0) {
        snprintf(lines[n++], APP_OVER_COLS, "nobody ever lived in this world");
    } else {
        snprintf(lines[n++], APP_OVER_COLS, "the last creature died in year %s",
                 when);
    }
    snprintf(lines[n++], APP_OVER_COLS, "seed 0x%" PRIx64, sim->seed);
    snprintf(lines[n++], APP_OVER_COLS, "most alive at once: %s", peak);
    if (r->peak_population == 0) {
        snprintf(lines[n++], APP_OVER_COLS, "oldest: nobody");
        snprintf(lines[n++], APP_OVER_COLS, "largest: nobody");
    } else {
        snprintf(lines[n++], APP_OVER_COLS, "oldest: #%u, %s",
                 (unsigned)r->oldest_id, age);
        snprintf(lines[n++], APP_OVER_COLS, "largest: #%u, size %.2f",
                 (unsigned)r->largest_id, (double)r->largest_size);
    }
    return n;
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
