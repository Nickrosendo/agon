#include "compare.h"

#include <stdbool.h>
#include <stdio.h> /* snprintf */

#include "actions.h"
#include "stats.h"
#include "tactics.h"
#include "years.h"

/* The chart's blocks, as in the skip summary: U+2581 (lowest) to U+2588
 * (full), three bytes of UTF-8 each, E2 96 81 to E2 96 88. */
#define BLOCK_BYTES 3
#define BLOCK_LOWEST 0x81

/* How many of the 16 tactics bits are set: each pass clears the lowest set
 * bit (x & (x - 1)), so the loop runs once per bit that was 1. */
static int bits_set(unsigned x)
{
    int n = 0;
    while (x != 0) {
        x &= x - 1;
        n++;
    }
    return n;
}

/* "+12 %", "-3 %", "same" or "" (no base to compare with): b against a.
 * long long: a count times 100 could pass an int on a very large world. */
static void change_text(int a, int b, char *out, size_t size)
{
    if (a <= 0) {
        snprintf(out, size, "%s", b > 0 ? "new" : "");
        return;
    }
    long long diff = (long long)(b - a) * 100;
    /* Rounded to the nearest whole percent, away from zero at the half. */
    long long pct = (diff + (diff >= 0 ? a / 2 : -(a / 2))) / a;
    if (pct == 0) {
        snprintf(out, size, "same");
    } else {
        snprintf(out, size, "%+lld %%", pct);
    }
}

/* One row of counts: a label, a's number, b's, and b against a. */
static void count_row(char *out, const char *label, int a, int b)
{
    char change[24];
    change_text(a, b, change, sizeof(change));
    snprintf(out, COMPARE_COLS, "  %-18s %8d %8d   %s", label, a, b, change);
}

/* Column c of cols from history h, as the average of the samples that fall
 * in it (the history has more samples than the chart has columns). */
static int column_value(const History *h, int c, int cols)
{
    /* Multiply before dividing: c / cols * len would be 0 for every c. */
    int start = c * h->len / cols;
    int end = (c + 1) * h->len / cols;
    if (end <= start) {
        end = start + 1;
    }
    long long sum = 0;
    for (int k = start; k < end; k++) {
        sum += history_at(h, k);
    }
    return (int)(sum / (end - start));
}

/*
 * Two chart lines, one per world, for the same count, drawn to one scale from
 * 0 to the highest number in either: a block half as tall is half as many.
 * Each world scaled to itself (as the skip summary does) would make a world
 * of 300 and one of 3,000 look the same, and a scale from the lowest number
 * would draw the smaller world as a flat line of the thinnest block. A 0 is a
 * space, so a group dying out shows as a gap.
 */
static int chart_pair(const char *label, const History *ha, char name_a,
                      const History *hb, char name_b, char lines[][COMPARE_COLS])
{
    const History *h[2] = {ha, hb};
    char names[2] = {name_a, name_b};
    /* The shorter history decides the columns, so both cover the same span:
     * the worlds sample on the same ticks, so the newest samples line up. */
    int len = ha->len < hb->len ? ha->len : hb->len;
    int cols = len < COMPARE_CHART_COLS ? len : COMPARE_CHART_COLS;
    if (cols == 0) {
        return 0;
    }
    int value[2][COMPARE_CHART_COLS];
    int top = 0;
    for (int w = 0; w < 2; w++) {
        /* The newest len samples of each: a copy of the history cut to len,
         * by skipping its oldest ones. History is a plain struct with an
         * array inside, so this assignment copies all of it. */
        History cut = *h[w];
        cut.len = len; /* history_at counts from len slots behind pos */
        for (int c = 0; c < cols; c++) {
            value[w][c] = column_value(&cut, c, cols);
            top = value[w][c] > top ? value[w][c] : top;
        }
    }
    for (int w = 0; w < 2; w++) {
        char *out = lines[w];
        int o = snprintf(out, COMPARE_COLS, "  %-16s %c ", label, names[w]);
        for (int c = 0; c < cols; c++) {
            if (o + BLOCK_BYTES + 1 >= COMPARE_COLS) {
                break;
            }
            if (value[w][c] <= 0) {
                out[o++] = ' ';
                continue;
            }
            /* 0..7 of the way from 0 to top, rounded down, so only the top
             * itself is a full block. top is above 0 here (this value is),
             * which matters: an int divided by 0 is undefined behaviour in C. */
            int height = value[w][c] * 7 / top;
            out[o++] = '\xE2';
            out[o++] = '\x96';
            out[o++] = (char)(BLOCK_LOWEST + height);
        }
        out[o] = '\0';
        if (w == 1 && o + 1 < COMPARE_COLS) {
            snprintf(out + o, (size_t)(COMPARE_COLS - o), "  (0 to %d, both)", top);
        }
    }
    return 2;
}

/* Whether gift i of log was given earlier in log, or is in other's log too
 * and will be told of from there: each gift is one row. */
static bool told_already(const ActionLog *log, int i, const ActionLog *other)
{
    if (actions_gift_repeats(log, i)) {
        return true;
    }
    if (other == NULL) {
        return false;
    }
    const PlayerAction *x = &log->items[i];
    for (int j = 0; j < other->count; j++) {
        const PlayerAction *y = &other->items[j];
        if (y->kind == ACTION_TRAIT && y->gene == x->gene && y->value == x->value) {
            return true;
        }
    }
    return false;
}

/* A row for each trait given in either world: how many carry it in each. */
static int gift_rows(const Sim *a, const Sim *b, char lines[][COMPARE_COLS], int max)
{
    int n = 0;
    const Sim *worlds[2] = {a, b};
    for (int w = 0; w < 2; w++) {
        const ActionLog *log = &worlds[w]->actions;
        /* b's gifts that a also has were told from a's log. */
        const ActionLog *before = w == 1 ? &a->actions : NULL;
        for (int i = 0; i < log->count && n < max; i++) {
            const PlayerAction *x = &log->items[i];
            if (x->kind != ACTION_TRAIT || told_already(log, i, before)) {
                continue;
            }
            char label[48];
            snprintf(label, sizeof(label), "%s %.2f given", actions_gene_name(x->gene),
                     (double)x->value);
            count_row(lines[n++], label, actions_carriers(a, x->gene, x->value),
                      actions_carriers(b, x->gene, x->value));
        }
    }
    return n;
}

int compare_lines(const Sim *a, char name_a, const Sim *b, char name_b,
                  uint64_t split_at, char lines[][COMPARE_COLS], int max)
{
    /* The longest the rows below can be, so a short max never cuts a table
     * in half: below that, nothing. */
    if (max < 12) {
        return 0;
    }
    int per_year = a->set.ticks_per_year;
    int n = 0;
    /* Years counted as the title bar counts them, from year 0. */
    snprintf(lines[n++], COMPARE_COLS, "%c (shown) against %c: split at year %d, "
                                       "now year %d",
             name_a, name_b, years_whole(split_at, per_year),
             years_whole(a->tick, per_year));
    bool behind = b->tick < a->tick && !sim_is_over(b);
    if (behind) {
        snprintf(lines[n++], COMPARE_COLS, "  %c is still catching up: its numbers "
                                           "are from year %d",
                 name_b, years_whole(b->tick, per_year));
    }
    snprintf(lines[n++], COMPARE_COLS, "  %-18s %8c %8c   %c against %c", "", name_a,
             name_b, name_b, name_a);
    const Stats *sa = &a->stats, *sb = &b->stats;
    count_row(lines[n++], "creatures", a->pool.count, b->pool.count);
    count_row(lines[n++], "plant-eaters", sa->by_level[BAND_PLANT],
              sb->by_level[BAND_PLANT]);
    count_row(lines[n++], "meat-eaters",
              sa->by_level[BAND_MEAT] + sa->by_level[BAND_TOP],
              sb->by_level[BAND_MEAT] + sb->by_level[BAND_TOP]);
    count_row(lines[n++], "planted cells", sa->planted, sb->planted);
    count_row(lines[n++], "families", sa->lineages, sb->lineages);
    count_row(lines[n++], "oldest (years)",
              years_whole(sa->oldest_age, per_year),
              years_whole(sb->oldest_age, per_year));
    /* Counted since the world began, so the years before the split are in
     * both: the difference is what came after. */
    count_row(lines[n++], "giants ever", (int)a->chronicle.giants,
              (int)b->chronicle.giants);
    count_row(lines[n++], "tactics seen", bits_set(a->tactics.seen),
              bits_set(b->tactics.seen));
    /* Room kept for the charts: a heading and two pairs. */
    n += gift_rows(a, b, lines + n, max - n - 5);
    const Sim *ended[2] = {a, b};
    char names[2] = {name_a, name_b};
    for (int w = 0; w < 2 && n < max - 5; w++) {
        if (sim_is_over(ended[w])) {
            snprintf(lines[n++], COMPARE_COLS, "  In %c the last creature died in "
                                               "year %d.",
                     names[w], years_whole(ended[w]->records.ended_at, per_year));
        }
    }
    /* The span the charts cover: the shorter history, in samples of
     * stats_every ticks, as years with one decimal. */
    int len = sa->hist_plant_eaters.len < sb->hist_plant_eaters.len
                  ? sa->hist_plant_eaters.len : sb->hist_plant_eaters.len;
    double years = (double)len * a->set.stats_every / per_year;
    /* Not while b is behind: its samples would be of another stretch of
     * time, and two lines side by side would seem to line up when they
     * do not. */
    if (behind && n < max) {
        snprintf(lines[n++], COMPARE_COLS, "  (charts once %c has caught up)", name_b);
    } else if (len > 0 && n < max - 4) {
        snprintf(lines[n++], COMPARE_COLS, "  The last %.1f years, one scale for "
                                           "both:", years);
        n += chart_pair("plant-eaters", &sa->hist_plant_eaters, name_a,
                        &sb->hist_plant_eaters, name_b, lines + n);
        n += chart_pair("meat-eaters", &sa->hist_meat_eaters, name_a,
                        &sb->hist_meat_eaters, name_b, lines + n);
    }
    return n;
}
