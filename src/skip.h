/*
 * skip: running a world years ahead without drawing it, a slice of ticks per
 * frame, and saying afterwards what happened while the player was away (the
 * player agency design, 6.4). Like the ancestors prelude, it only calls
 * sim_step, so a skipped world is the same as one watched the whole time.
 */
#ifndef AGON_SKIP_H
#define AGON_SKIP_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "sim.h"

/* One column's worth of the population chart: the numbers at the start of a
 * season. */
typedef struct {
    uint64_t tick;
    int population, plant_eaters, meat_eaters, planted;
} SkipSample;

/* Room for each line of the summary, in bytes; SKIP_LINES lines at most.
 * Bytes, not columns: a chart block is 3 bytes of UTF-8 and one column on
 * screen. SKIP_CHART_COLS: the widest chart, in columns. */
enum { SKIP_COLS = 160, SKIP_LINES = 16, SKIP_CHART_COLS = 40 };

typedef struct {
    bool active;    /* still running */
    bool cancelled; /* stopped before its end */
    uint64_t from, until;
    /* samples[0] is the start, then one per season boundary passed. On the
     * heap, room for samples_cap of them, owned by the Skip. */
    SkipSample *samples;
    int n_samples, samples_cap;
    /* The world as it was at the start, for the summary to compare with. */
    int start_lineages;
    uint32_t start_giants;
    uint16_t start_tactics;
} Skip;

/* Starts a skip of years years from s's tick. False for years below 1, a
 * world already over, or no memory; the Skip then holds nothing. A Skip
 * already holding a summary is emptied first. */
bool skip_start(Skip *k, const Sim *s, int years);
/* Runs ticks until budget_ms milliseconds by now_ms have passed, the skip has
 * reached its end or the world has died. Returns whether it is still on. */
bool skip_step(Skip *k, Sim *s, uint64_t (*now_ms)(void), uint64_t budget_ms);
/* Runs the rest at once: for tests and runs that must not depend on a clock. */
void skip_finish(Skip *k, Sim *s);
/* Stops where it got to; the summary covers what was run. */
void skip_cancel(Skip *k);
/* "skipping ahead: year 3 of 20, about 1 min left (Esc stops)". elapsed_ms:
 * how long the skip has been running, for the estimate. */
void skip_progress_text(const Skip *k, const Sim *s, uint64_t elapsed_ms,
                        char *out, size_t size);
/* Fills the "while you were away" lines, a chart of creatures, plant-eaters
 * and meat-eaters among them, and returns how many. */
int skip_summary_lines(const Skip *k, const Sim *s,
                       char lines[][SKIP_COLS], int max);
/* Frees the chart. The Skip can be started again afterwards. */
void skip_free(Skip *k);

#endif
