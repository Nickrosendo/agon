#include "panel.h"

#include <assert.h>   /* static_assert */
#include <inttypes.h> /* PRId64 */
#include <stdio.h>    /* snprintf */
#include <string.h>   /* strlen */

#include "cells.h"
#include "chronicle.h"
#include "palette.h"
#include "years.h"

/* No swatch on this line: a heading or a blank line. */
#define NO_SWATCH -1

/* Columns before a line's text: a space, then a swatch two columns
 * wide. */
#define SWATCH_INDENT 3

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
            /* Banded by the hue drawn on the map, so each group's name
             * describes the colour you see. */
            int band = hue_band(render_display_hue(hue));
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

void panel_format_count(char *out, size_t size, int64_t n)
{
    if (size == 0) {
        return;
    }
    /* The plain digits first. An int64_t has at most 19 digits and a sign,
     * so 24 bytes always fit; snprintf returns how many characters it
     * wrote (not counting the '\0'). PRId64 is the right printf letters for
     * an int64_t on this system: "ld" on Linux, "lld" on Windows. */
    char digits[24];
    int len = snprintf(digits, sizeof(digits), "%" PRId64, n);
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

/* The eight block heights, U+2581 (lowest) to U+2588 (full). In UTF-8
 * each is the three bytes E2 96 xx, with xx running from 81 to 88. */
#define BLOCK_BYTES 3
static const char BLOCK_LEAD[2] = {'\xE2', '\x96'};
#define BLOCK_LOWEST 0x81

/* The average of the samples column c of cols covers, newest column on
 * the right; -1 when none of them exists yet. */
static int column_average(const History *h, int c, int cols)
{
    int per = HISTORY / cols;
    int end = h->len - (cols - 1 - c) * per; /* one past the last */
    int start = end - per;
    if (start < 0) {
        start = 0;
    }
    if (end <= 0) {
        return -1;
    }
    /* Not long: that is 32 bits on Windows, and a big world's plant counts
     * added up would pass 2 billion. */
    int64_t sum = 0;
    for (int k = start; k < end; k++) {
        sum += history_at(h, k);
    }
    return (int)(sum / (end - start));
}

void panel_sparkline(char *out, size_t size, const History *h, int cols)
{
    if (size == 0) {
        return;
    }
    out[0] = '\0';
    /* A graph has at most one column per sample, so HISTORY entries are
     * always enough. cols is checked first: avg[cols] past the end would
     * not be caught by the compiler. */
    if (cols < 1 || cols > HISTORY) {
        return;
    }
    int avg[HISTORY];
    int max = 0;
    for (int c = 0; c < cols; c++) {
        avg[c] = column_average(h, c, cols);
        if (avg[c] > max) {
            max = avg[c];
        }
    }
    size_t o = 0;
    for (int c = 0; c < cols; c++) {
        if (avg[c] <= 0) {
            if (o + 1 >= size) {
                break;
            }
            out[o] = ' ';
            o++;
            continue;
        }
        /* 0..7, rounded: the largest column (avg == max) gets 7. avg > 0
         * here, so max > 0 and the division is safe. */
        int height = (avg[c] * 7 + max / 2) / max;
        if (o + BLOCK_BYTES >= size) {
            break; /* never write part of a character */
        }
        out[o] = BLOCK_LEAD[0];
        out[o + 1] = BLOCK_LEAD[1];
        /* char may be signed: build the byte as an int, then convert. */
        out[o + 2] = (char)(BLOCK_LOWEST + height);
        o += BLOCK_BYTES;
    }
    out[o] = '\0';
}

/* What a line's colour cell shows: a RENDER_SWATCH_ value, or one of
 * these. */
#define HUE_SWATCH -2   /* the creature colour given by the line's hue */
#define BLANK_SWATCH -3 /* an empty cell */

/* One line of the panel: two colour cells, a label and a count (-1 for
 * none). A line whose first swatch is NO_SWATCH is a heading or blank
 * line: just the label, from the first column. A line with a graph shows
 * only that history's graph. */
typedef struct {
    int swatch, swatch2;
    float hue, hue2; /* for HUE_SWATCH cells */
    float meat;      /* for HUE_SWATCH cells: 0 plant-eater .. 1 meat */
    const char *label;
    int64_t count;
    /* Text to show in place of the count, or NULL. Only a pointer: the characters
     * belong to the caller, and must outlast the drawing of this line. */
    const char *value;
    const History *graph; /* NULL for an ordinary line */
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
    if (line->graph != NULL) {
        /* 3 bytes per block, so 16 columns take up to 48 bytes: the
         * byte length of UTF-8 text is not its width on screen. */
        char graph[3 * PANEL_GRAPH_COLS + 1];
        panel_sparkline(graph, sizeof(graph), line->graph, PANEL_GRAPH_COLS);
        render_text(col, row, "", SWATCH_INDENT);
        render_utf8(col + SWATCH_INDENT, row, graph);
        render_text(col + SWATCH_INDENT + PANEL_GRAPH_COLS, row, "",
                    PANEL_COLS - SWATCH_INDENT - PANEL_GRAPH_COLS);
        return;
    }
    if (line->swatch == NO_SWATCH) {
        render_text(col, row, line->label, PANEL_COLS);
        return;
    }
    render_text(col, row, " ", 1);
    draw_cell(line->swatch, line->hue, line->meat, col + 1, row);
    draw_cell(line->swatch2, line->hue2, line->meat, col + 2, row);
    if (line->value != NULL) {
        /* The label's padding shrinks as the value grows, so the two together
         * always fill the same 16 columns. The * takes the width from the
         * argument list; a label longer than that just pushes the value right,
         * and render_text cuts the end. */
        int label_cols = 16 - (int)strlen(line->value);
        snprintf(text, sizeof(text), " %-*s%s", label_cols > 0 ? label_cols : 0,
                 line->label, line->value);
    } else if (line->count < 0) {
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

/* A history graph. */
static PanelLine graph_line(const History *h)
{
    return (PanelLine){.swatch = NO_SWATCH, .label = "", .count = -1,
                       .graph = h};
}

/* A label and a count with no colour cells. */
static PanelLine count_line(const char *label, int64_t count)
{
    return (PanelLine){
        .swatch = BLANK_SWATCH, .swatch2 = BLANK_SWATCH, .label = label,
        .count = count};
}

/* A line with two fixed colours. */
static PanelLine swatch_line(int swatch, int swatch2, const char *label,
                             int64_t count)
{
    return (PanelLine){
        .swatch = swatch, .swatch2 = swatch2, .label = label, .count = count};
}

/* The creatures line: its cells show the two largest colour groups (the
 * same colour twice when there is one group, blank when there is none). */
static PanelLine creatures_line(const ColourGroup *groups, int n_groups,
                                int64_t count)
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
                                      const char *label, int64_t count)
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

/* Room for every line panel_draw can make: 18 fixed lines, 2 more before
 * the groups, the refused births and one per group. */
#define PANEL_MAX_LINES (21 + PANEL_GROUP_LINES)

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
    const Stats *st = &s->stats;
    ColourGroup groups[PANEL_GROUP_LINES];
    int n_groups = panel_colour_groups(s, groups, PANEL_GROUP_LINES);

    /* Built line by line, because how many there are depends on how many
     * colour groups exist this frame. Each line is a whole PanelLine
     * returned by value and copied into the array. */
    PanelLine lines[PANEL_MAX_LINES];
    int n = 0;
    add_line(lines, &n, heading(" population"));
    add_line(lines, &n, creatures_line(groups, n_groups, st->creatures));
    /* A plant-eater keeps its family's pale colour (shown here in the
     * largest group's); a meat-eater turns deep red. */
    float family_hue = n_groups > 0 ? groups[0].hue : 0.33f;
    add_line(lines, &n,
             creature_colour_line(family_hue, 0.0f, "eat plants",
                                  st->by_level[BAND_PLANT]));
    add_line(lines, &n, graph_line(&st->hist_plant_eaters));
    add_line(lines, &n,
             creature_colour_line(0.0f, 1.0f, "eat meat",
                                  st->by_level[BAND_MEAT] +
                                      st->by_level[BAND_TOP]));
    add_line(lines, &n, graph_line(&st->hist_meat_eaters));
    add_line(lines, &n, count_line("top hunter", st->by_level[BAND_TOP]));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_PLANT, RENDER_SWATCH_PLANT, "plants",
                         st->planted));
    add_line(lines, &n, graph_line(&st->hist_planted));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_WITHERED, RENDER_SWATCH_WITHERED,
                         "withering", st->withering));
    add_line(lines, &n,
             swatch_line(RENDER_SWATCH_CARRION, RENDER_SWATCH_CARRION,
                         "carrion", st->carrion_cells));
    add_line(lines, &n, count_line("families", st->lineages));
    /* uint32_t into int64_t: every uint32_t value fits, on every system. */
    /* Outlives the line that points at it: both last until the end of this
     * function, where the lines have been drawn. */
    char oldest[32];
    years_format(oldest, sizeof(oldest), st->oldest_age, s->set.ticks_per_year);
    PanelLine oldest_line = count_line("oldest", 0);
    oldest_line.value = oldest;
    add_line(lines, &n, oldest_line);
    /* Only when it has happened: a full pool must never be quiet. */
    if (s->pool.births_refused > 0) {
        add_line(lines, &n, count_line("refused", s->pool.births_refused));
    }
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

/* What the brain told a creature to do, by ACT_ value. */
static const char *const ACTION_NAMES[] = {
    "rest", "turn left", "turn right", "move", "bite", "mate", "attack",
};
/* sizeof the whole array divided by sizeof one element is its length,
 * which only works on a real array (not on a pointer to one). */
static_assert(sizeof(ACTION_NAMES) / sizeof(ACTION_NAMES[0]) == ACT_COUNT,
              "one name per action");

/*
 * The lines being built: a pointer to the caller's two-dimensional array, rows
 * of PANEL_LINE_BYTES chars in one block of memory, and how many there is room
 * for. text[k] is row k, itself an array of chars, which can be passed to
 * snprintf as a char pointer.
 */
typedef struct {
    char (*text)[PANEL_LINE_BYTES]; /* a pointer to rows, not an array of pointers */
    int max;
    int n;
} Lines;

/* Adds a line: " key" on the left, value right-aligned so the line is
 * PANEL_COLS - 1 wide. A line that does not fit is dropped. */
static void add_pair(Lines *l, const char *key, const char *value)
{
    if (l->n < l->max) {
        /* %-11s pads the key to 11, %8s right-aligns the value in 8:
         * 1 + 11 + 8 = 20 = PANEL_COLS. A longer value pushes past it, and
         * the line is cut at PANEL_COLS below. */
        snprintf(l->text[l->n], PANEL_LINE_BYTES, " %-11s%8s", key, value);
        l->text[l->n][PANEL_COLS] = '\0';
        l->n++;
    }
}

static void add_text(Lines *l, const char *text)
{
    if (l->n < l->max) {
        snprintf(l->text[l->n], PANEL_LINE_BYTES, "%s", text);
        /* Cut to the panel's width. PANEL_COLS < PANEL_LINE_BYTES, so this
         * index is inside the row; past a shorter text it is harmless. */
        l->text[l->n][PANEL_COLS] = '\0';
        l->n++;
    }
}

/* A float with the given number of decimals, as text in out. */
static void format_float(char *out, size_t size, float v, int decimals)
{
    /* "%.*f": the * takes the number of decimals from the next argument,
     * an int, instead of from the format text. A float argument is
     * passed to snprintf as a double, which is what %f expects. */
    snprintf(out, size, "%.*f", decimals, (double)v);
}

static void add_float(Lines *l, const char *key, float v, int decimals)
{
    char value[24];
    format_float(value, sizeof(value), v, decimals);
    add_pair(l, key, value);
}

static void add_count(Lines *l, const char *key, int64_t v)
{
    char value[24];
    panel_format_count(value, sizeof(value), v);
    add_pair(l, key, value);
}

static void describe_cell(Lines *l, const Sim *s, const Inspect *in)
{
    const World *w = &s->world;
    int i = world_idx(w, in->x, in->y);
    char text[PANEL_LINE_BYTES];
    snprintf(text, sizeof(text), " cell %d, %d", in->x, in->y);
    add_text(l, text);
    if (w->terrain[i] == TERRAIN_WATER) {
        add_pair(l, "ground", "water");
    } else if (w->terrain[i] == TERRAIN_SHALLOW) {
        add_pair(l, "ground", "shallow water");
    } else if (w->terrain[i] == TERRAIN_ROCK) {
        add_pair(l, "ground", "rock");
    } else {
        add_float(l, "fertility", w->fertility[i], 2);
        add_float(l, "water", w->water[i], 2);
        add_count(l, "plants", w->veg[i]);
        if (w->veg[i] > 0) {
            const Plant *plant = &w->plant[i];
            add_float(l, "plant energy", plant->energy, 1);
            add_float(l, "fruit", plant->fruit, 1);
            add_float(l, "fruit share", plant->fruit_share, 2);
            add_float(l, "water need", plant->water_need, 2);
            add_float(l, "best temp", plant->temp_pref, 2);
        }
        if (w->carrion[i] > 0.0f) {
            add_float(l, "carrion", w->carrion[i], 1);
        }
    }
}

static void describe_creature(Lines *l, const Sim *s, const Creature *c)
{
    const Settings *set = &s->set;
    char text[PANEL_LINE_BYTES];
    /* %u for unsigned int; uint32_t is converted to it explicitly, since
     * C does not promise that uint32_t is unsigned int on every system. */
    snprintf(text, sizeof(text), " creature #%u", (unsigned)c->id);
    add_text(l, text);
    snprintf(text, sizeof(text), " family   #%u", (unsigned)c->lineage);
    add_text(l, text);
    add_float(l, "energy", c->energy, 1);
    add_float(l, "level", c->level, 2);
    add_float(l, "food eaten", c->eaten, 0);
    add_pair(l, "hunter", creature_is_hunter(c) ? "yes" : "no");
    add_float(l, "size", creature_size(c, set), 2);
    add_float(l, "speed", creature_speed(c, set), 2);
    add_count(l, "sight", creature_sight(c, set));
    add_float(l, "leaf gene", c->genome.g[GENE_LEAF], 2);
    add_float(l, "fruit gene", c->genome.g[GENE_FRUIT], 2);
    add_float(l, "meat gene", c->genome.g[GENE_MEAT], 2);
    add_float(l, "ageing gene", c->genome.g[GENE_AGEING], 2);
    add_float(l, "armour gene", c->genome.g[GENE_ARMOUR], 2);
    add_float(l, "weapon gene", c->genome.g[GENE_WEAPON], 2);
    add_float(l, "growth gene", c->genome.g[GENE_GROWTH], 2);
    add_float(l, "territory gene", c->genome.g[GENE_TERRITORY], 2);
    add_float(l, "legs gene", c->genome.g[GENE_LEGS], 2);
    add_float(l, "swim gene", c->genome.g[GENE_SWIM], 2);
    add_float(l, "upkeep", creature_upkeep_cost(c, set), 2);
    /* kind comes from a brain; checked before indexing the array. */
    const char *doing =
        c->current.kind < ACT_COUNT ? ACTION_NAMES[c->current.kind] : "?";
    add_pair(l, "doing", doing);
}

/* The top of the panel for a creature: who it is, and the newest few things that
 * happened to it, newest first so a short panel cuts the oldest. */
static void describe_life(Lines *l, const Sim *s, const Creature *c)
{
    char name[16], text[PANEL_LINE_BYTES];
    name_from_id(c->id, name, sizeof(name));
    snprintf(text, sizeof(text), " %s", name);
    add_text(l, text);
    const char *title = chronicle_title(&c->tally);
    if (title[0] != '\0') {
        snprintf(text, sizeof(text), " %s", title);
        add_text(l, text);
    }
    name_from_id(c->lineage, name, sizeof(name));
    snprintf(text, sizeof(text), " family %s", name);
    add_text(l, text);
    char age[32];
    years_format(age, sizeof(age), c->age, s->set.ticks_per_year);
    add_pair(l, "age", age);
    add_float(l, "health", c->health, 1);
    add_count(l, "kills", c->tally.kills);
    /* The ring holds the newest CREATURE_EVENTS; the newest is at
     * (events_noted - 1) % CREATURE_EVENTS. k is at most kept, which is at most
     * events_noted, so the unsigned events_noted - k never goes below 0: if it
     * did, it would wrap round to about 4 billion instead. */
    uint32_t kept = c->events_noted < CREATURE_EVENTS ? c->events_noted
                                                      : CREATURE_EVENTS;
    for (uint32_t k = 1; k <= kept && k <= PANEL_LOG_LINES; k++) {
        const LifeEvent *e = &c->events[(c->events_noted - k) % CREATURE_EVENTS];
        char line[PANEL_LINE_BYTES - 1];
        chronicle_short_line(e, s->set.ticks_per_year, line, sizeof(line));
        snprintf(text, sizeof(text), " %s", line);
        add_text(l, text);
    }
}

int panel_inspect_text(const Sim *s, const Inspect *in,
                       char lines[][PANEL_LINE_BYTES], int max)
{
    /* lines arrives as a pointer to its first row: the [] in the parameter
     * means "pointer", and the row length is what lets lines[k] find row k. */
    Lines l = {.text = lines, .max = max, .n = 0};
    const Creature *c = inspect_creature(in, s);
    if (c != NULL) {
        describe_life(&l, s, c);
        add_text(&l, "");
    } else {
        add_text(&l, " inspect");
    }
    describe_cell(&l, s, in);
    add_text(&l, "");
    if (c != NULL) {
        describe_creature(&l, s, c);
    } else {
        add_text(&l, " no creature here");
    }
    return l.n;
}

void panel_draw_inspect(const Sim *s, const Inspect *in, int col, int row0,
                        int rows)
{
    /* About 3 KB on the stack, the same as the old Lines struct. */
    char lines[PANEL_INSPECT_LINES][PANEL_LINE_BYTES];
    int n = panel_inspect_text(s, in, lines, PANEL_INSPECT_LINES);
    for (int r = 0; r < rows; r++) {
        render_text(col, row0 + r, r < n ? lines[r] : "", PANEL_COLS);
    }
}

void render_too_small(int cols, int rows, int min_cols, int min_rows)
{
    render_text(0, 0, "window too small", cols);
    if (rows > 1) {
        char need[48];
        snprintf(need, sizeof(need), "need %d x %d", min_cols, min_rows);
        render_text(0, 1, need, cols);
    }
}
