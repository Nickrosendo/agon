#include "skip.h"

#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* realloc, free */

#include "actions.h"
#include "tactics.h"
#include "years.h" /* years_season_of */

/* What the world counts now: the chart's numbers for one moment. */
static SkipSample sample_now(const Sim *s)
{
    /* The latest sample (stats.h), at most stats_every ticks old. */
    const Stats *st = &s->stats;
    return (SkipSample){
        .tick = s->tick,
        .population = s->pool.count,
        .plant_eaters = st->by_level[BAND_PLANT],
        .meat_eaters = st->by_level[BAND_MEAT] + st->by_level[BAND_TOP],
        .planted = st->planted,
    };
}

static bool add_sample(Skip *k, const Sim *s)
{
    if (k->n_samples == k->samples_cap) {
        int bigger = k->samples_cap == 0 ? 32 : k->samples_cap * 2;
        /* realloc into a new pointer: on failure it returns NULL and leaves
         * the old block where it was, still ours to use and free. */
        SkipSample *grown =
            realloc(k->samples, (size_t)bigger * sizeof(*grown));
        if (grown == NULL) {
            return false; /* the chart misses a column; the skip goes on */
        }
        k->samples = grown;
        k->samples_cap = bigger;
    }
    k->samples[k->n_samples] = sample_now(s);
    k->n_samples++;
    return true;
}

void skip_free(Skip *k)
{
    free(k->samples);
    *k = (Skip){0};
}

bool skip_start(Skip *k, const Sim *s, int years)
{
    skip_free(k);
    if (years < 1 || sim_is_over(s)) {
        return false;
    }
    k->from = s->tick;
    k->until = s->tick + (uint64_t)years * (uint64_t)s->set.ticks_per_year;
    k->start_lineages = s->stats.lineages;
    k->start_giants = s->chronicle.giants;
    k->start_tactics = s->tactics.seen;
    if (!add_sample(k, s)) {
        return false;
    }
    k->active = true;
    return true;
}

static void skip_tick(Skip *k, Sim *s)
{
    int season = years_season_of(s->tick, s->set.ticks_per_year);
    sim_step(s);
    if (years_season_of(s->tick, s->set.ticks_per_year) != season) {
        add_sample(k, s);
    }
    if (s->tick >= k->until || sim_is_over(s)) {
        k->active = false;
    }
}

bool skip_step(Skip *k, Sim *s, uint64_t (*now_ms)(void), uint64_t budget_ms)
{
    uint64_t start = now_ms();
    while (k->active) {
        skip_tick(k, s);
        /* Unsigned subtraction: right even if the clock wrapped round. */
        if (now_ms() - start >= budget_ms) {
            break;
        }
    }
    return k->active;
}

void skip_finish(Skip *k, Sim *s)
{
    while (k->active) {
        skip_tick(k, s);
    }
}

void skip_cancel(Skip *k)
{
    if (k->active) {
        k->active = false;
        k->cancelled = true;
    }
}

static int year_of(uint64_t tick, const Sim *s)
{
    return (int)(tick / (uint64_t)s->set.ticks_per_year);
}

void skip_progress_text(const Skip *k, const Sim *s, uint64_t elapsed_ms,
                        char *out, size_t size)
{
    uint64_t total = k->until - k->from;
    uint64_t done = s->tick - k->from;
    int years = (int)(total / (uint64_t)s->set.ticks_per_year);
    int year = (int)(done / (uint64_t)s->set.ticks_per_year) + 1;
    if (year > years) {
        year = years;
    }
    if (done == 0 || elapsed_ms < 1000) {
        snprintf(out, size, "skipping ahead: year %d of %d (Esc stops)", year,
                 years);
        return;
    }
    /* Time left at the speed so far. The product fits easily: a day of
     * milliseconds times a few million ticks is far below 2^64. */
    uint64_t left_ms = elapsed_ms * (total - done) / done;
    uint64_t left_s = left_ms / 1000;
    if (left_s < 60) {
        snprintf(out, size,
                 "skipping ahead: year %d of %d, about %d s left (Esc stops)",
                 year, years, (int)left_s);
    } else {
        snprintf(out, size,
                 "skipping ahead: year %d of %d, about %d min left (Esc stops)",
                 year, years, (int)((left_s + 30) / 60));
    }
}

/* The chart's blocks: U+2581 (lowest) to U+2588 (full), three bytes of
 * UTF-8 each, E2 96 81 to E2 96 88; the same ones as the panel's graphs. */
#define BLOCK_BYTES 3
#define BLOCK_LOWEST 0x81

enum { CHART_POPULATION, CHART_PLANT, CHART_MEAT, CHART_COUNT };

static int sample_value(const SkipSample *x, int which)
{
    switch (which) {
    case CHART_PLANT: return x->plant_eaters;
    case CHART_MEAT: return x->meat_eaters;
    default: return x->population;
    }
}

/* Sample i of the chart: the Skip's own samples, then the world now as one
 * more when it is past the last of them (a stopped skip, or a world that
 * died between seasons), so a chart ends where the world is. */
static int sample_at(const Skip *k, const SkipSample *now, int i, int which)
{
    return sample_value(i < k->n_samples ? &k->samples[i] : now, which);
}

/* Column c of cols, as the average of the n samples that fall in it. With
 * fewer samples than columns, each sample is one column (cols was cut to
 * n). */
static int column_value(const Skip *k, const SkipSample *now, int n, int c,
                        int cols, int which)
{
    /* Multiply before dividing: integer division drops the remainder, so
     * c / cols * n would be 0 for every c below cols. */
    int start = c * n / cols;
    int end = (c + 1) * n / cols;
    if (end <= start) {
        end = start + 1;
    }
    long long sum = 0;
    for (int i = start; i < end; i++) {
        sum += sample_at(k, now, i, which);
    }
    return (int)(sum / (end - start));
}

/* One chart line: a label, a block per column, and the line's lowest and
 * highest numbers. The blocks run from the lowest (the shortest block) to the
 * highest (the full one), not from zero: a count that moves between 1,700
 * and 3,400 would otherwise fill the top half of every block and look flat.
 * A count of 0 is a space, so a group dying out shows as a gap. */
static void chart_line(const Skip *k, const SkipSample *now, int n,
                       int which, char *out, size_t size)
{
    static const char *const LABELS[CHART_COUNT] = {
        "creatures    ", "plant-eaters ", "meat-eaters  "};
    int cols = n < SKIP_CHART_COLS ? n : SKIP_CHART_COLS;
    int value[SKIP_CHART_COLS];
    int low = 0, top = 0;
    for (int c = 0; c < cols; c++) {
        value[c] = column_value(k, now, n, c, cols, which);
        if (c == 0 || value[c] < low) {
            low = value[c];
        }
        top = value[c] > top ? value[c] : top;
    }
    int range = top - low;
    int o = snprintf(out, size, "%s", LABELS[which]);
    for (int c = 0; c < cols; c++) {
        /* Room for a whole character and the final '\0', or stop: never
         * write half of one. o is an int and size a size_t, so o is
         * converted before comparing; o is never negative here. */
        if ((size_t)o + BLOCK_BYTES + 1 >= size) {
            break;
        }
        if (value[c] <= 0) {
            out[o++] = ' ';
            continue;
        }
        /* 0..7, rounded; a line that never changed sits in the middle. The
         * range is checked first: dividing an int by 0 is undefined
         * behaviour in C, not an error or Infinity as in TypeScript. */
        int height = range == 0 ? 3 : ((value[c] - low) * 7 + range / 2) / range;
        out[o++] = '\xE2';
        out[o++] = '\x96';
        /* char may be signed: build the byte as an int, then convert. */
        out[o++] = (char)(BLOCK_LOWEST + height);
    }
    out[o] = '\0';
    if ((size_t)o + 1 < size) {
        snprintf(out + o, size - (size_t)o, " %d to %d", low, top);
    }
}

/* The chart: a heading saying how long a column is, then a line for each
 * count. Returns how many lines it took (0 without room or samples). */
static int chart_lines(const Skip *k, const Sim *s, char lines[][SKIP_COLS],
                       int max)
{
    if (max < 1 + CHART_COUNT || k->n_samples < 1) {
        return 0;
    }
    SkipSample now = sample_now(s);
    bool now_is_new = now.tick != k->samples[k->n_samples - 1].tick;
    int n = k->n_samples + (now_is_new ? 1 : 0);
    int cols = n < SKIP_CHART_COLS ? n : SKIP_CHART_COLS;
    /* Seasons a column covers, in tenths, rounded; 4 seasons a year. */
    int tenths = (n * 10 + cols / 2) / cols;
    if (tenths <= 10) {
        snprintf(lines[0], SKIP_COLS, "Each column a season:");
    } else {
        snprintf(lines[0], SKIP_COLS, "Each column about %.1f years:",
                 (double)tenths / 40.0);
    }
    for (int which = 0; which < CHART_COUNT; which++) {
        chart_line(k, &now, n, which, lines[1 + which], SKIP_COLS);
    }
    return 1 + CHART_COUNT;
}

int skip_summary_lines(const Skip *k, const Sim *s, char lines[][SKIP_COLS],
                       int max)
{
    int n = 0;
    int years_done = year_of(s->tick - k->from, s);
    int years_asked = year_of(k->until - k->from, s);
    if (n < max && k->cancelled) {
        snprintf(lines[n++], SKIP_COLS,
                 "Stopped after %d of %d years (now year %d).", years_done,
                 years_asked, year_of(s->tick, s) + 1);
    } else if (n < max) {
        snprintf(lines[n++], SKIP_COLS,
                 "While you were away: %d %s (now year %d).", years_done,
                 years_done == 1 ? "year" : "years", year_of(s->tick, s) + 1);
    }
    int most = s->pool.count;
    for (int i = 0; i < k->n_samples; i++) {
        if (k->samples[i].population > most) {
            most = k->samples[i].population;
        }
    }
    if (n < max && k->n_samples > 0) {
        snprintf(lines[n++], SKIP_COLS,
                 "Creatures: %d then, %d now; at most %d at a season's start.",
                 k->samples[0].population, s->pool.count, most);
    }
    n += chart_lines(k, s, lines + n, max - n);
    if (n < max && sim_is_over(s)) {
        snprintf(lines[n++], SKIP_COLS, "The last creature died in year %d.",
                 year_of(s->records.ended_at, s) + 1);
    }
    if (n < max) {
        snprintf(lines[n++], SKIP_COLS, "Families: %d then, %d now.",
                 k->start_lineages, s->stats.lineages);
    }
    uint32_t giants = s->chronicle.giants - k->start_giants;
    if (n < max && giants > 0) {
        snprintf(lines[n++], SKIP_COLS, "%u %s grew to giant size.",
                 (unsigned)giants, giants == 1 ? "creature" : "creatures");
    }
    /* Tactics seen now and not at the start: ~ flips every bit, so
     * seen & ~start keeps the bits that are new. */
    uint16_t fresh = (uint16_t)(s->tactics.seen & ~k->start_tactics);
    for (int t = 0; t < TACTIC_COUNT && n < max; t++) {
        if (fresh & (1u << t)) {
            snprintf(lines[n++], SKIP_COLS, "A family took up a tactic: %s.",
                     tactics_name(t));
        }
    }
    const ActionLog *log = &s->actions;
    for (int i = 0; i < log->count && n < max; i++) {
        const PlayerAction *a = &log->items[i];
        if (a->kind != ACTION_TRAIT || actions_gift_repeats(log, i)) {
            continue;
        }
        int now = actions_carriers(s, a->gene, a->value);
        if (now == 0) {
            snprintf(lines[n++], SKIP_COLS, "The %s you gave (%.2f) died out.",
                     actions_gene_name(a->gene), (double)a->value);
        } else {
            snprintf(lines[n++], SKIP_COLS,
                     "The %s you gave (%.2f): %d carry it now.",
                     actions_gene_name(a->gene), (double)a->value, now);
        }
    }
    return n;
}
