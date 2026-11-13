/*
 * chronicle: what really happened to each creature. Every creature carries a
 * small ring of its newest events and a few counts (LifeTally, creature.h); the
 * chronicle adds a fixed table of the most notable lives, the dead included, with
 * longer stories. Nothing here is read by a rule, draws a random number or
 * allocates after chronicle_init, so recording cannot change a world.
 *
 * Lore states facts and never a motive: "attacked by Doru", then "moved into the
 * cold lands", and the reader draws the conclusion.
 */
#ifndef AGON_CHRONICLE_H
#define AGON_CHRONICLE_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "creature.h"

/* What a LifeEvent records. other_id and value mean, for each: */
enum {
    LIFE_BORN,        /* other: a parent (none for a founder); value: 1 born, 0 founder */
    LIFE_CHILD,       /* other: the child */
    LIFE_ATTACKED,    /* the first hit of a fight; other: the attacker */
    LIFE_SURVIVED,    /* FIGHT_GAP ticks unhit since; other: the last attacker */
    LIFE_WOUNDED,     /* health fell below HEAVY_WOUND; other: the attacker; value: % left */
    LIFE_KILL,        /* other: the one killed; value: which kill, 1 for the first */
    LIFE_KILL_HUNTER, /* the same, the one killed being a hunter */
    LIFE_KIN_LOST,    /* a relative died within KIN_NEAR cells; other: that relative */
    LIFE_MOVED,       /* settled in another region; value: the REGION_ */
    LIFE_LARGEST,     /* became the largest alive; value: size in tenths */
    LIFE_OLDEST,      /* became the oldest alive */
    LIFE_GIANT,       /* reached giant_size; value: size in tenths */
    LIFE_DIED,        /* value: a DeathCause; other: the killer, for wounds */
    LIFE_SLEPT,       /* woke from a deep sleep of SLEEP_NOTED ticks or more; value: its ticks */
    LIFE_TACTIC,      /* its family's tactic was confirmed; other: the family's
                       * lineage; value: tactics_pack's tactic and two counts */
    LIFE_COUNT
};

/* A fight is over once FIGHT_GAP ticks pass with no hit; a hit after a longer
 * quiet starts a new one. The same 100 ticks the window draws scars for. */
#define FIGHT_GAP 100
/* A hit that leaves less than this share of an unhurt body's health is a heavy
 * wound. */
#define HEAVY_WOUND 0.25f
/* How near, in cells (a square), a death counts as a relative lost nearby. */
#define KIN_NEAR 3
/* Ticks a creature must stand in a new region before it counts as moved there,
 * so that walking along a border does not fill the story with crossings. */
#define REGION_SETTLE 50
/* The shortest deep sleep the story records, in ticks: a sleep is one event,
 * noted on waking, and shorter naps would fill the ring with nothing. */
#define SLEEP_NOTED 30

/* How many lives the table keeps, and how many events each keeps. */
#define CHRONICLE_NOTABLE 64
#define NOTABLE_EVENTS 48

/* One notable life. While alive is true it is kept up to date at each of its
 * events; after its death it stays as it was. Events as in a creature's ring. */
typedef struct {
    bool used;
    bool alive;
    uint32_t id, lineage, age;
    float score;
    LifeTally tally;
    uint32_t events_noted;
    LifeEvent events[NOTABLE_EVENTS];
} NotableLife;

/* The struct name after "struct" matches the one creature.h declares, so a
 * Pool's struct Chronicle * and a Chronicle * are the same type. */
typedef struct Chronicle {
    NotableLife *lives; /* CHRONICLE_NOTABLE entries, allocated once */
    /* How many creatures have grown to giant size in this world, and the id of
     * the newest: what the notices watch (notices.h). Not saved, so they
     * start again from 0 after a load; the notices start afresh too. */
    uint32_t giants;
    uint32_t newest_giant;
} Chronicle;

/* Allocates the table, empty. False when memory runs out; nothing to free. */
bool chronicle_init(Chronicle *c);
/* Empties the table, for a new world. */
void chronicle_clear(Chronicle *c);
/* Frees the table; safe to call twice and on a zeroed Chronicle. */
void chronicle_free(Chronicle *c);

/*
 * Records e in who's ring, updates who's tally by e's kind, and updates or
 * claims who's entry in the table: a creature enters when its notability is
 * above the least notable entry's (an unused one counts as below everything),
 * bringing the events still in its ring. Does nothing when c is NULL, so callers
 * can pass a Pool's chronicle as it is. For LIFE_KILL and LIFE_KILL_HUNTER,
 * e.value is replaced by the kill's number.
 */
void chronicle_note(Chronicle *c, Creature *who, LifeEvent e);

/* The table's entry for id, or NULL. A loan: valid until the next note. */
const NotableLife *chronicle_find(const Chronicle *c, uint32_t id);

/* How notable a life is, from its counts and age. Higher is more notable. */
float chronicle_notability(const LifeTally *t, uint32_t age);

/* A name made from an id alone, like "Varo" or "Kelmisa": the same id always
 * gives the same name, and two ids can share one. Always ends with '\0' within
 * len. */
void name_from_id(uint32_t id, char *out, size_t len);

/* A title earned by real deeds, such as "Breaker of Hunters", or "" for none.
 * A constant the caller must not change or free. */
const char *chronicle_title(const LifeTally *t);

/* One event as a line of text, with its date in years: "year 7: killed Doru,
 * kill 3". t is the tally of the creature the event belongs to (a birth names
 * both parents from it). Always ends with '\0' within len. */
void chronicle_line(const LifeEvent *e, const LifeTally *t, int ticks_per_year,
                    char *out, size_t len);

/* The same event in at most 19 characters when names are short, for the panel's
 * narrow log: "y52 killed Thakou". Longer names are cut by whoever draws it.
 * Facts only, as for chronicle_line. Always ends with '\0' within len. */
void chronicle_short_line(const LifeEvent *e, int ticks_per_year, char *out,
                          size_t len);

#endif
