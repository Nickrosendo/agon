#include "panel.h"

#include <stdio.h> /* snprintf */

#include "render.h"

/* No swatch on this line: a heading or a blank line. */
#define NO_SWATCH -1

/* Columns before a line's text: a space, then a swatch two columns
 * wide. */
#define SWATCH_INDENT 3

PanelCounts panel_count(const Sim *s)
{
    const World *w = &s->world;
    PanelCounts c = {0};
    c.creatures = s->pool.count;
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->veg[i] > 0) {
            c.plants++;
            if (w->veg_wither[i] > 0) {
                c.withering++;
            }
        }
    }
    return c;
}

/* The band a hue falls in. A hue is below 1, but clamping costs nothing
 * and keeps a stray 1.0 from indexing one past the end of the arrays. */
static int hue_band(float hue)
{
    int band = (int)(hue * (float)PANEL_HUE_BANDS);
    if (band < 0) {
        return 0;
    }
    return band >= PANEL_HUE_BANDS ? PANEL_HUE_BANDS - 1 : band;
}

int panel_common_hues(const Sim *s, float *hues, int n)
{
    int count[PANEL_HUE_BANDS] = {0};
    float sum[PANEL_HUE_BANDS] = {0};
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (c->alive) {
            float hue = c->genome.g[GENE_HUE];
            int band = hue_band(hue);
            count[band]++;
            sum[band] += hue;
        }
    }
    /* Pick the largest band n times, emptying each once picked. n is at
     * most a handful, so this beats sorting. */
    int written = 0;
    while (written < n) {
        int best = -1;
        for (int b = 0; b < PANEL_HUE_BANDS; b++) {
            /* > rather than >=: on a tie the lower band stays best. */
            if (count[b] > 0 && (best < 0 || count[b] > count[best])) {
                best = b;
            }
        }
        if (best < 0) {
            break; /* no occupied band left */
        }
        hues[written] = sum[best] / (float)count[best];
        count[best] = 0;
        written++;
    }
    return written;
}

void panel_format_count(char *out, size_t size, long n)
{
    if (size == 0) {
        return;
    }
    /* The plain digits first. A long has at most 19 digits and a sign,
     * so 24 bytes always fit; snprintf returns how many characters it
     * wrote (not counting the '\0'). */
    char digits[24];
    int len = snprintf(digits, sizeof(digits), "%ld", n);
    int first = digits[0] == '-' ? 1 : 0; /* where the digits start */

    size_t o = 0;
    for (int i = 0; i < len && o + 1 < size; i++) {
        /* A comma before every digit whose count of digits still to come
         * is a multiple of three, except before the first digit. */
        int left = len - i;
        if (i > first && left % 3 == 0) {
            out[o] = ',';
            o++;
            if (o + 1 >= size) {
                break; /* no room left for the digit itself */
            }
        }
        out[o] = digits[i];
        o++;
    }
    out[o] = '\0';
}

/* One line of the panel: a swatch (a RENDER_SWATCH_ value, or
 * NO_SWATCH), a label and a count (-1 for none). */
typedef struct {
    int swatch, swatch2; /* swatch2: a second colour beside the first */
    const char *label;
    long count;
} PanelLine;

/* A swatch value meaning "the creatures' colours": the line's two cells
 * show the two most common creature hues instead of fixed colours. */
#define HUE_SWATCH -2

/* The two most common creature hues, for HUE_SWATCH lines: count of them
 * (0, 1 or 2) are valid. */
typedef struct {
    float hue[2];
    int count;
} CommonHues;

/* The line's two colour cells, at columns col and col + 1. */
static void draw_swatches(const PanelLine *line, const CommonHues *common,
                          int col, int row)
{
    if (line->swatch != HUE_SWATCH) {
        render_swatch(col, row, line->swatch);
        render_swatch(col + 1, row, line->swatch2);
        return;
    }
    for (int k = 0; k < 2; k++) {
        if (k < common->count) {
            render_hue_swatch(col + k, row, common->hue[k]);
        } else if (common->count == 1) {
            render_hue_swatch(col + k, row, common->hue[0]); /* one colour */
        } else {
            render_text(col + k, row, " ", 1); /* nobody alive */
        }
    }
}

static void draw_line(const PanelLine *line, const CommonHues *common,
                      int col, int row)
{
    /* Roomier than the panel: render_text cuts the line to its width
     * anyway, and gcc's -Wformat-truncation checks that snprintf can
     * never be forced to cut a count short. */
    char text[32];
    if (line->swatch == NO_SWATCH) {
        render_text(col, row, line->label, PANEL_COLS);
        return;
    }
    render_text(col, row, " ", 1);
    draw_swatches(line, common, col + 1, row);
    if (line->count < 0) {
        snprintf(text, sizeof(text), " %s", line->label);
    } else {
        char count[16]; /* "-9,223,372,036,854,775,808" would not fit,
                           but no count here comes near it */
        panel_format_count(count, sizeof(count), line->count);
        /* %-9s pads the label with spaces to 9 columns, %7s right-aligns
         * the count in 7: 1 + 9 + 7 = 17 = PANEL_COLS - SWATCH_INDENT. */
        snprintf(text, sizeof(text), " %-9s%7s", line->label, count);
    }
    render_text(col + SWATCH_INDENT, row, text, PANEL_COLS - SWATCH_INDENT);
}

void panel_draw(const Sim *s, int col, int row0, int rows)
{
    PanelCounts c = panel_count(s);
    CommonHues common;
    /* common.hue is an array inside a struct; passed to the function it
     * becomes a pointer to its first element, room for 2 floats. */
    common.count = panel_common_hues(s, common.hue, 2);
    /* A local array of structs, built anew on every call: unlike a
     * static table, its initialiser may use values computed at run
     * time, such as this frame's counts. */
    const PanelLine lines[] = {
        {NO_SWATCH, NO_SWATCH, " population", -1},
        {HUE_SWATCH, HUE_SWATCH, "creatures", c.creatures},
        {RENDER_SWATCH_PLANT, RENDER_SWATCH_PLANT, "plants", c.plants},
        {RENDER_SWATCH_WITHERED, RENDER_SWATCH_WITHERED, "withering",
         c.withering},
        {NO_SWATCH, NO_SWATCH, "", -1},
        {NO_SWATCH, NO_SWATCH, " map", -1},
        {RENDER_SWATCH_WATER, RENDER_SWATCH_WATER, "water", -1},
        {RENDER_SWATCH_ROCK, RENDER_SWATCH_ROCK, "rock", -1},
        {RENDER_SWATCH_SOIL_DRY, RENDER_SWATCH_SOIL_RICH, "soil, dry/rich",
         -1},
    };
    int count = (int)(sizeof(lines) / sizeof(lines[0]));
    for (int r = 0; r < rows; r++) {
        if (r < count) {
            draw_line(&lines[r], &common, col, row0 + r);
        } else {
            render_text(col, row0 + r, "", PANEL_COLS); /* blank */
        }
    }
}
