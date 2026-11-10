/*
 * panel: the column on the right of the world. The population from the
 * latest statistics sample, with history graphs, the food chain, a
 * legend of the map's colours and the commonest family colours.
 */
#ifndef AGON_PANEL_H
#define AGON_PANEL_H

#include <stddef.h> /* size_t */
#include <stdint.h> /* int64_t */

#include "inspect.h"
#include "sim.h"

/* How many screen columns the panel takes. */
#define PANEL_COLS 20

/* The rows of panel_draw that can be clicked, counted from its top. Fixed, since
 * only the lines after these depend on the colour groups. wtest_cells.c checks
 * that the words really are on these rows. */
#define PANEL_ROW_TOP_HUNTER 6
#define PANEL_ROW_OLDEST 12

/* How many bands the hue wheel is cut into when counting colours. */
#define PANEL_HUE_BANDS 12

/* How many colour groups the panel lists, most common first. */
#define PANEL_GROUP_LINES 6

/* The creatures whose hue falls in one band of the colour wheel. */
typedef struct {
    int band;    /* 0 .. PANEL_HUE_BANDS - 1, red first */
    float hue;   /* the average inherited hue of its members */
    int count;   /* how many are alive */
} ColourGroup;

/* Finds the n largest colour groups among living creatures: the hue
 * wheel, as drawn (render_display_hue), is cut into PANEL_HUE_BANDS bands, and the bands holding the most
 * creatures win (on a tie, the band with the lower hue). Writes them into
 * groups[0], groups[1], ..., largest first, and returns how many it
 * wrote: fewer than n when fewer bands are occupied, 0 (groups untouched)
 * when nobody is alive. groups must have room for n entries. */
int panel_colour_groups(const Sim *s, ColourGroup *groups, int n);

/* A short name for a band's colour ("red", "violet", ...), or "?" for a
 * number outside 0 .. PANEL_HUE_BANDS - 1. */
const char *panel_band_name(int band);

/* Writes n with a comma every three digits ("12,345") into out, a buffer
 * of size bytes. Always ends out with '\0', cutting the text short if it
 * does not fit; does nothing when size is 0. int64_t rather than long: long
 * is 64 bits on Linux but 32 on Windows, where a tick count would be cut. */
void panel_format_count(char *out, size_t size, int64_t n);

/* How many screen columns a history graph takes. Each column shows the
 * average of HISTORY / PANEL_GRAPH_COLS samples. */
#define PANEL_GRAPH_COLS 16

/* Writes a graph of the history h, cols characters wide, into out (a
 * buffer of size bytes) as UTF-8: the newest samples on the right, each
 * column the average of HISTORY / cols samples, drawn as one of eight
 * block heights scaled to the largest column. A column with no samples
 * yet, or whose average is 0, is a space. Each block takes 3 bytes, so
 * out needs 3 * cols + 1 bytes; with less, it holds as many whole
 * characters as fit. Always ends out with '\0' (unless size is 0). cols
 * must divide HISTORY. */
/* The tactics some living family shows now, by name and joined by commas
 * ("night, pack"), or "none yet". Always ends out with '\0' within size. */
void tactics_list(const Sim *s, char *out, size_t size);
void panel_sparkline(char *out, size_t size, const History *h, int cols);

/* Draws the panel in the screen area starting at (col, row0), PANEL_COLS
 * wide and rows tall. Lines that do not fit are left out. */
void panel_draw(const Sim *s, int col, int row0, int rows);

/* How many lines panel_inspect_text can make, and the bytes each takes:
 * PANEL_COLS characters and the '\0', with room to spare so gcc's
 * -Wformat-truncation can see snprintf never has to cut a value. */
#define PANEL_INSPECT_LINES 64
#define PANEL_LINE_BYTES 48
/* How many of a creature's newest events the panel's log shows. */
#define PANEL_LOG_LINES 5

/* Inspect mode's panel as text: when a creature is under the cursor, first its
 * life (name, title, family, age, health, kills and its newest events, newest
 * first), then the cell, then the creature's body and genes. Writes at most max
 * lines into lines, each at most PANEL_COLS characters, and returns how many. */
int panel_inspect_text(const Sim *s, const Inspect *in,
                       char lines[][PANEL_LINE_BYTES], int max);

/* Draws inspect mode's panel in the same area: panel_inspect_text's lines, as
 * many as fit. */
void panel_draw_inspect(const Sim *s, const Inspect *in, int col, int row0,
                        int rows);

/* The message shown instead of the world when the window is too small. Here
 * and not in render.c because both programs show it, and it needs only cells. */
void render_too_small(int cols, int rows, int min_cols, int min_rows);

#endif
