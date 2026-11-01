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

static void draw_line(const PanelLine *line, int col, int row)
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
    render_swatch(col + 1, row, line->swatch);
    render_swatch(col + 2, row, line->swatch2);
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
    /* A local array of structs, built anew on every call: unlike a
     * static table, its initialiser may use values computed at run
     * time, such as this frame's counts. */
    const PanelLine lines[] = {
        {NO_SWATCH, NO_SWATCH, " population", -1},
        {RENDER_SWATCH_CREATURE, RENDER_SWATCH_CREATURE, "creatures",
         c.creatures},
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
            draw_line(&lines[r], col, row0 + r);
        } else {
            render_text(col, row0 + r, "", PANEL_COLS); /* blank */
        }
    }
}
