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
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *cr = &s->pool.items[i];
        if (!cr->alive) {
            continue;
        }
        if (cr->level >= PANEL_MEAT_LEVEL) {
            c.meat_eaters++;
        } else {
            c.plant_eaters++;
        }
    }
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->carrion[i] > 0.0f) {
            c.carrion++;
        }
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

/* One name per band, 30 degrees of the wheel each, red first. The
 * colours on screen are pale (see render.c), so the names are rough. */
static const char *const BAND_NAMES[PANEL_HUE_BANDS] = {
    "red",  "orange", "yellow", "lime",   "green",   "mint",
    "cyan", "azure",  "blue",   "violet", "magenta", "pink",
};

const char *panel_band_name(int band)
{
    if (band < 0 || band >= PANEL_HUE_BANDS) {
        return "?"; /* BAND_NAMES[band] would read outside the array */
    }
    return BAND_NAMES[band];
}

int panel_colour_groups(const Sim *s, ColourGroup *groups, int n)
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
        groups[written] = (ColourGroup){
            .band = best,
            .hue = sum[best] / (float)count[best],
            .count = count[best],
        };
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

/* What a line's colour cell shows: a RENDER_SWATCH_ value, or one of
 * these. */
#define HUE_SWATCH -2   /* the creature colour given by the line's hue */
#define BLANK_SWATCH -3 /* an empty cell */

/* One line of the panel: two colour cells, a label and a count (-1 for
 * none). A line whose first swatch is NO_SWATCH is a heading or blank
 * line: just the label, from the first column. */
typedef struct {
    int swatch, swatch2;
    float hue, hue2; /* for HUE_SWATCH cells */
    float meat;      /* for HUE_SWATCH cells: 0 plant-eater .. 1 meat */
    const char *label;
    long count;
} PanelLine;

static void draw_cell(int swatch, float hue, float meat, int col, int row)
{
    if (swatch == HUE_SWATCH) {
        render_creature_swatch(col, row, hue, meat);
    } else if (swatch >= 0) {
        render_swatch(col, row, swatch);
    } else {
        render_text(col, row, " ", 1);
    }
}

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
    draw_cell(line->swatch, line->hue, line->meat, col + 1, row);
    draw_cell(line->swatch2, line->hue2, line->meat, col + 2, row);
    if (line->count < 0) {
        snprintf(text, sizeof(text), " %s", line->label);
    } else {
        char count[16]; /* "-9,223,372,036,854,775,808" would not fit,
                           but no count here comes near it */
        panel_format_count(count, sizeof(count), line->count);
        /* %-10s pads the label with spaces to 10 columns, %6s
         * right-aligns the count in 6 ("99,999" at most fits): 1 + 10 + 6
         * = 17 = PANEL_COLS - SWATCH_INDENT. */
        snprintf(text, sizeof(text), " %-10s%6s", line->label, count);
    }
    render_text(col + SWATCH_INDENT, row, text, PANEL_COLS - SWATCH_INDENT);
}

/* Lines with no colour cells: a heading, or "" for a blank line. */
static PanelLine heading(const char *label)
{
    return (PanelLine){.swatch = NO_SWATCH, .label = label, .count = -1};
}

/* A line with two fixed colours. */
static PanelLine swatch_line(int swatch, int swatch2, const char *label,
                             long count)
{
    return (PanelLine){
        .swatch = swatch, .swatch2 = swatch2, .label = label, .count = count};
}

/* The creatures line: its cells show the two largest colour groups (the
 * same colour twice when there is one group, blank when there is none). */
static PanelLine creatures_line(const ColourGroup *groups, int n_groups,
                                long count)
{
    PanelLine line = swatch_line(BLANK_SWATCH, BLANK_SWATCH, "creatures",
                                 count);
    if (n_groups >= 1) {
        line.swatch = line.swatch2 = HUE_SWATCH;
        line.hue = line.hue2 = groups[0].hue;
    }
    if (n_groups >= 2) {
        line.hue2 = groups[1].hue;
    }
    return line;
}

/* A line whose two cells show a creature colour. */
static PanelLine creature_colour_line(float hue, float meat,
                                      const char *label, long count)
{
    PanelLine line = swatch_line(HUE_SWATCH, HUE_SWATCH, label, count);
    line.hue = line.hue2 = hue;
    line.meat = meat;
    return line;
}

/* One colour group: both cells in its colour, its name and its count. */
static PanelLine group_line(const ColourGroup *g)
{
    return creature_colour_line(g->hue, 0.0f, panel_band_name(g->band),
                                g->count);
}

/* Room for every line panel_draw can make: 12 fixed lines, 2 more before
 * the groups and one per group. */
#define PANEL_MAX_LINES (14 + PANEL_GROUP_LINES)

/* Appends line to lines, which holds *n lines so far, and counts it. A
 * line that does not fit is dropped: C would let lines[*n] be written
 * past the end of the array without a word. */
static void add_line(PanelLine *lines, int *n, PanelLine line)
{
    if (*n < PANEL_MAX_LINES) {
        lines[*n] = line;
        (*n)++; /* the parentheses: *n++ would move the pointer instead */
    }
}

void panel_draw(const Sim *s, int col, int row0, int rows)
{
    PanelCounts c = panel_count(s);
    ColourGroup groups[PANEL_GROUP_LINES];
    int n_groups = panel_colour_groups(s, groups, PANEL_GROUP_LINES);

    /* Built line by line, because how many there are depends on how many
     * colour groups exist this frame. Each line is a whole PanelLine
     * returned by value and copied into the array. */
    PanelLine lines[PANEL_MAX_LINES];
    int n = 0;
    add_line(lines, &n, heading(" population"));
    add_line(lines, &n, creatures_line(groups, n_groups, c.creatures));
    /* A plant-eater keeps its family's pale colour (shown here in the
     * largest group's); a meat-eater turns deep red. */
    float family_hue = n_groups > 0 ? groups[0].hue : 0.33f;
    add_line(lines, &n, creature_colour_line(family_hue, 0.0f, "eat plants",
                                             c.plant_eaters));
    add_line(lines, &n,
             creature_colour_line(0.0f, 1.0f, "eat meat", c.meat_eaters));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_PLANT, RENDER_SWATCH_PLANT, "plants",
                         c.plants));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_WITHERED, RENDER_SWATCH_WITHERED,
                         "withering", c.withering));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_CARRION, RENDER_SWATCH_CARRION,
                         "carrion", c.carrion));
    add_line(lines, &n, heading(""));
    add_line(lines, &n, heading(" map"));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_WATER, RENDER_SWATCH_WATER, "water",
                         -1));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_ROCK, RENDER_SWATCH_ROCK, "rock", -1));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_SOIL_DRY, RENDER_SWATCH_SOIL_RICH,
                         "soil, dry/rich", -1));
    /* Last, so a short window cuts the groups rather than the legend. */
    add_line(lines, &n, heading(""));
    /* Grouped by inherited hue, before any red shift for meat. */
    add_line(lines, &n, heading(" family colours"));
    for (int k = 0; k < n_groups; k++) {
        add_line(lines, &n, group_line(&groups[k]));
    }
    for (int r = 0; r < rows; r++) {
        if (r < n) {
            draw_line(&lines[r], col, row0 + r);
        } else {
            render_text(col, row0 + r, "", PANEL_COLS); /* blank */
        }
    }
}
