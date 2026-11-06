/*
 * lifepage: the chronicle page the window opens with L, and the announcement
 * when a creature first grows to giant size. Only state and text here: the
 * window draws the lines (window_main.c), and the tests read them.
 *
 * The page names a life by its id, never by a pointer or a slot: a creature's
 * slot is given to a newborn the moment it dies. Each frame the page copies what
 * it shows from the living creature or from the chronicle's table, and when both
 * are gone it keeps the last copy, so a death never empties an open page.
 */
#ifndef AGON_LIFEPAGE_H
#define AGON_LIFEPAGE_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "chronicle.h"
#include "sim.h"

/* One line of the page as text: room for a wide window, and the '\0'. */
#define LIFE_PAGE_LINE_BYTES 128
/* How many lines life_page_text can make at most: a heading of 7 lines, every
 * event a notable life keeps, and the closing line. */
#define LIFE_PAGE_MAX_LINES (8 + NOTABLE_EVENTS)

typedef struct {
    bool open;
    uint32_t id;
    /* Whether anything is known of this life: false for an id that was never
     * alive or recorded. */
    bool known;
    bool alive;
    bool in_table; /* the events come from the chronicle's table */
    uint32_t lineage, age;
    float size, health;
    LifeTally tally;
    uint32_t events_noted; /* every event of the life, kept or not */
    int n_events;          /* how many are in events[], oldest first */
    LifeEvent events[NOTABLE_EVENTS];
} LifePage;

/* Opens the page on the life with this id and reads it at once. */
void life_page_open(LifePage *p, const Sim *s, uint32_t id);
/* Opens the page on the most notable life in the chronicle's table, the dead
 * included. False, with the page left closed, when the table is empty. */
bool life_page_open_most_notable(LifePage *p, const Sim *s);
void life_page_close(LifePage *p);
/* Reads the life again: from its creature while it lives, events from the
 * table when it has an entry there, else from the creature's own ring. When
 * neither has it any more, what was read last stays, marked as dead. */
void life_page_update(LifePage *p, const Sim *s);

/* The page as lines of text, at most max_lines of them, each cut to cols
 * characters. When the events do not all fit, the newest are kept and a line
 * says how many earlier ones are left out. Returns how many lines it wrote. */
int life_page_text(const LifePage *p, int ticks_per_year, int cols,
                   char lines[][LIFE_PAGE_LINE_BYTES], int max_lines);

/* ---- the giant announcement ---- */

typedef struct {
    uint32_t seen;    /* the chronicle's giants count when last checked */
    uint32_t id;      /* the giant last announced, for jumping to it */
    bool has_giant;   /* id means something */
} Announce;

/* Forgets what was seen, for a new world. */
void announce_reset(Announce *a);
/*
 * True once for each creature that grew to giant size since the last call, with
 * its words in out ("a giant: Raikai of the Sodri family, year 52"). When several
 * did between two calls, only the newest is announced. Needs the chronicle
 * setting on: the announcement is the chronicle's LIFE_GIANT event.
 */
bool announce_check(Announce *a, const Sim *s, char *out, size_t len);
/* The slot of the giant last announced, or -1 when there is none or it has
 * died. */
int announce_slot(const Announce *a, const Sim *s);

/* The slot of the living creature with this id, or -1. */
int life_find_slot(const Sim *s, uint32_t id);

#endif
