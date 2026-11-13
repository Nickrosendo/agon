/*
 * notices: short lines about rare things as they happen, for both modes (the
 * player agency design, section 5): a body unlike its family's, the first
 * giant and each new record size, a tactic seen for the first time, a big family gone, and a trait the player gave
 * spreading past NOTICE_GIFT_SPREAD carriers or dying out.
 *
 * Like the chronicle and the tactic watch, it only reads the world: it takes a
 * const Sim *, draws no random number and allocates nothing, so checking can
 * never change what happens. It belongs to the screen, not to the world, and is
 * not saved: after a load or a new world the window starts it again, and its
 * first check takes what is already there as known.
 *
 * Notices holds no pointers, only numbers and fixed arrays, so copying it by
 * assignment makes a whole independent copy (a branch gets one that way).
 */
#ifndef AGON_NOTICES_H
#define AGON_NOTICES_H

#include <stdbool.h>
#include <stdint.h>

#include "sim.h"

/* How many notices the list keeps; older ones fall off its end. */
#define NOTICES_KEPT 32
/* Room for one notice's words, the closing '\0' included. */
#define NOTICE_TEXT 96

/* The body genes watched, and how many there are. Speed, sight and the
 * givable traits: genes a body shows. Digestion is left out (it follows the
 * diet), and so are hue, which wraps round, and the genes that shape choices. */
extern const int NOTICE_GENE_LIST[];
enum { NOTICE_GENES = 9 };
/* A body is unlike its family when one of its watched genes is this far from
 * the family's average. Measured 2026-11-13: genes past 0.95 or below 0.05
 * are common in every world from its first year (genes are clamped at 0 and
 * 1, so they pile up there), so being near an end says nothing; standing far
 * from one's own family does. */
#define NOTICE_UNLIKE 0.6f
/* Only families of this many living members have an average worth comparing
 * with. */
#define NOTICE_LIKE_FAMILY 10
/* An unlike body is news only while at most this many living bodies are so
 * on that gene: many at once is a family splitting, not a rare body. */
#define NOTICE_RARE 3
/* After the first giant, a new one is news only when the largest body alive
 * beats the record by this share (0.1: a tenth larger). */
#define NOTICE_RECORD_STEP 0.1f
/* A family is big, and its end is news, from this many living members. */
#define NOTICE_FAMILY_MIN 20
/* How many big families are remembered at once. */
#define NOTICE_FAMILIES 32
/* A given trait is news once more than this many carry it. */
#define NOTICE_GIFT_SPREAD 10
/* How many of the action log's first entries are followed. */
#define NOTICE_GIFTS 64

/* Notice.creature for a notice about nobody to follow. Not 0: the first
 * creature of a world has id 0. Ids count up from there, one per birth, and
 * a world would need four billion births to reach this one. */
#define NOTICE_NOBODY UINT32_MAX

typedef struct {
    uint64_t tick;
    uint32_t creature; /* who it is about, or NOTICE_NOBODY */
    char text[NOTICE_TEXT];
} Notice;

/* A family that has had NOTICE_FAMILY_MIN members or more. */
typedef struct {
    uint32_t lineage;
    int peak; /* the most members it had at a check; 0: the entry is unused */
} BigFamily;

/* A gift's state, as bits of one byte. */
enum {
    GIFT_CARRIED = 1 << 0, /* someone carried it at a check */
    GIFT_SPREAD = 1 << 1,  /* its spreading was told */
    GIFT_GONE = 1 << 2     /* its end was told */
};

typedef struct {
    bool started;          /* the first check, which tells nothing, has run */
    uint64_t last_sample;  /* the stats tick of the last check */
    /* The list: newest at items[(total - 1) % NOTICES_KEPT]. */
    Notice items[NOTICES_KEPT];
    uint32_t total;        /* notices ever added */
    uint32_t giants_seen;  /* the chronicle's giant count at the last check */
    float record_size;     /* the largest body told of; 0 before the first giant */
    uint16_t tactics_told; /* bit k: tactic k's first sight was told */
    /* For each watched gene: whether some living body is unlike its family
     * on it, and the tick from which a new one is news again (a year after
     * the last one went), so a body wavering at the line is told once. */
    bool unlike_here[NOTICE_GENES];
    uint64_t unlike_news_from[NOTICE_GENES];
    BigFamily families[NOTICE_FAMILIES];
    uint8_t gifts[NOTICE_GIFTS]; /* GIFT_ bits, by log entry */
} Notices;

/* Empty, as for a new world: the next check tells nothing. */
void notices_reset(Notices *n);
/* Looks at the world when a new sample has been taken since the last call
 * (s->stats.tick moved), and adds a notice for each new rare thing. Cheap to
 * call every frame: between samples it returns at once. */
void notices_check(Notices *n, const Sim *s);
/* Adds one notice; text is cut to fit. */
void notices_add(Notices *n, uint64_t tick, uint32_t creature,
                 const char *text);
/* The notice back places from the newest (0 is the newest), or NULL when
 * there are not that many. A loan: valid until the next notice is added. */
const Notice *notices_get(const Notices *n, uint32_t back);
const Notice *notices_newest(const Notices *n);

#endif
