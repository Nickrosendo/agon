/*
 * panel: the column on the right of the world. For now a legend of the
 * map's colours (the creatures' two most common colours included) and
 * the current population; lesson 8 adds history
 * graphs, the food chain and inspect mode.
 */
#ifndef AGON_PANEL_H
#define AGON_PANEL_H

#include <stddef.h> /* size_t */

#include "sim.h"

/* How many screen columns the panel takes. */
#define PANEL_COLS 20

typedef struct {
    int creatures;
    int plants;    /* cells with any vegetation */
    int withering; /* of those, how many are withering */
} PanelCounts;

/* Counts what the panel shows, straight from the world and the pool. */
PanelCounts panel_count(const Sim *s);

/* How many bands the hue wheel is cut into when counting colours. */
#define PANEL_HUE_BANDS 12

/* Finds the n most common colours among living creatures: the hue wheel
 * is cut into PANEL_HUE_BANDS bands, and the bands holding the most
 * creatures win (on a tie, the band with the lower hue). Writes the
 * average hue of each winning band into hues[0], hues[1], ..., most
 * common first, and returns how many it wrote: fewer than n when fewer
 * bands are occupied, 0 (hues untouched) when nobody is alive. hues must
 * have room for n floats. */
int panel_common_hues(const Sim *s, float *hues, int n);

/* Writes n with a comma every three digits ("12,345") into out, a buffer
 * of size bytes. Always ends out with '\0', cutting the text short if it
 * does not fit; does nothing when size is 0. */
void panel_format_count(char *out, size_t size, long n);

/* Draws the panel in the screen area starting at (col, row0), PANEL_COLS
 * wide and rows tall. Lines that do not fit are left out. */
void panel_draw(const Sim *s, int col, int row0, int rows);

#endif
