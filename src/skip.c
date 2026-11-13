#include "skip.h"

#include <math.h>   /* fabsf */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* realloc, free */

#include "actions.h"
#include "tactics.h"

/* A creature counts as carrying a given trait while its gene is this close
 * to the value given: mutation moves genes a little in every generation. */
#define CARRIER_TOLERANCE 0.05f

static bool add_year(Skip *k, const Sim *s)
{
    if (k->n_years == k->years_cap) {
        int bigger = k->years_cap == 0 ? 32 : k->years_cap * 2;
        YearCount *grown = realloc(k->years, (size_t)bigger * sizeof(*grown));
        if (grown == NULL) {
            return false; /* the chart misses a row; the skip goes on */
        }
        k->years = grown;
        k->years_cap = bigger;
    }
    /* The latest sample (stats.h), at most stats_every ticks old. */
    const Stats *st = &s->stats;
    k->years[k->n_years] = (YearCount){
        .population = s->pool.count,
        .plant_eaters = st->by_level[BAND_PLANT],
        .meat_eaters = st->by_level[BAND_MEAT] + st->by_level[BAND_TOP],
        .planted = st->planted,
    };
    k->n_years++;
    return true;
}

void skip_free(Skip *k)
{
    free(k->years);
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
    if (!add_year(k, s)) {
        return false;
    }
    k->active = true;
    return true;
}

static void skip_tick(Skip *k, Sim *s)
{
    sim_step(s);
    if (s->tick % (uint64_t)s->set.ticks_per_year == 0) {
        add_year(k, s);
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

/* Living creatures whose gene is within CARRIER_TOLERANCE of value. */
static int carriers(const Sim *s, int gene, float value)
{
    int n = 0;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        n += c->alive && fabsf(c->genome.g[gene] - value) < CARRIER_TOLERANCE;
    }
    return n;
}

/* Whether log entry i gives the same gene and value as an earlier entry, so
 * each given trait is told once however many creatures got it. */
static bool told_before(const ActionLog *log, int i)
{
    const PlayerAction *a = &log->items[i];
    for (int j = 0; j < i; j++) {
        const PlayerAction *b = &log->items[j];
        if (b->kind == ACTION_TRAIT && b->gene == a->gene &&
            b->value == a->value) {
            return true;
        }
    }
    return false;
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
    for (int i = 0; i < k->n_years; i++) {
        if (k->years[i].population > most) {
            most = k->years[i].population;
        }
    }
    if (n < max && k->n_years > 0) {
        snprintf(lines[n++], SKIP_COLS,
                 "Creatures: %d then, %d now; at most %d at a year's start.",
                 k->years[0].population, s->pool.count, most);
    }
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
        if (a->kind != ACTION_TRAIT || told_before(log, i)) {
            continue;
        }
        int now = carriers(s, a->gene, a->value);
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
