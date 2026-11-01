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
    int plant_eaters; /* creatures below PANEL_MEAT_LEVEL */
    int meat_eaters;  /* creatures at or above it */
    int carrion;      /* cells with carrion on them */
} PanelCounts;

/* The food-chain level from which the panel counts a creature as a
 * meat-eater: half way from a pure plant-eater (2) to a pure eater of
 * plant-eaters (3), so more than half its food was meat. */
#define PANEL_MEAT_LEVEL 2.5f

/* Counts what the panel shows, straight from the world and the pool. */
PanelCounts panel_count(const Sim *s);

/* How many bands the hue wheel is cut into when counting colours. */
#define PANEL_HUE_BANDS 12

/* How many colour groups the panel lists, most common first. */
#define PANEL_GROUP_LINES 6

/* The creatures whose hue falls in one band of the colour wheel. */
typedef struct {
    int band;    /* 0 .. PANEL_HUE_BANDS - 1, red first */
    float hue;   /* the average hue of its members */
    int count;   /* how many are alive */
} ColourGroup;

/* Finds the n largest colour groups among living creatures: the hue
 * wheel is cut into PANEL_HUE_BANDS bands, and the bands holding the most
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
 * does not fit; does nothing when size is 0. */
void panel_format_count(char *out, size_t size, long n);

/* Draws the panel in the screen area starting at (col, row0), PANEL_COLS
 * wide and rows tall. Lines that do not fit are left out. */
void panel_draw(const Sim *s, int col, int row0, int rows);

#endif
