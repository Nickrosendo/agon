/*
 * lookahead: the Documentary camera that knows what is coming (the player
 * agency design, section 5). A copy of the world on screen, made through the
 * save format (sim_copy), runs a season ahead of it. Every creature alive on
 * screen is scored by what its life gains in that copy (a first kill, growing
 * giant, young), and the camera follows the best, saying why in one line.
 *
 * Watching is free: the look-ahead only reads the world on screen (a const
 * Sim *) and steps its own copy, so the shown world unfolds exactly as it
 * would without it. The copy is the real future only while nobody acts on
 * the shown world; any new action in its log, a new seed or a world put back
 * in time drops the copy and the next sync makes a fresh one.
 */
#ifndef AGON_LOOKAHEAD_H
#define AGON_LOOKAHEAD_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "creature.h" /* LifeTally */
#include "sim.h"

/* "Nobody": ids start at 0, so the largest uint32_t is the one never used. */
#define LOOKAHEAD_NOBODY UINT32_MAX
/* Room for the reason line, the closing '\0' included. */
#define LOOKAHEAD_WHY 96

typedef struct {
    bool has;            /* ahead holds a copy, owned by the Lookahead */
    Sim ahead;
    uint64_t lead;       /* how many ticks ahead of the shown world */
    /* What the shown world was when copied, to tell when the copy is no
     * longer its future: its seed, its tick, and how many actions it had. */
    uint64_t seed, copied_at;
    int actions_seen;
} Lookahead;

/* Empty, with nothing to free yet; lead in ticks (a season is
 * ticks_per_year / 4). */
void lookahead_init(Lookahead *l, uint64_t lead);
/* The copy exists, is still the shown world's future, and has run its lead
 * ahead (or its world has ended, so there is nothing more to run). */
bool lookahead_ready(const Lookahead *l, const Sim *shown);
/* Makes a fresh copy if there is none or it is no longer the future, then
 * steps the copy towards shown->tick + lead until budget_ms by now_ms has
 * passed. False, with a one-line message, only when a copy could not be made
 * (memory ran out); the Lookahead is then empty. */
bool lookahead_sync(Lookahead *l, const Sim *shown, uint64_t (*now_ms)(void),
                    uint64_t budget_ms, char *err, size_t len);
/* The same with no clock, to the end: for tests and runs that must not
 * depend on time. */
void lookahead_sync_all(Lookahead *l, const Sim *shown);
/* Why a pick was made: the biggest part of its coming season. */
typedef enum {
    LOOK_QUIET,
    LOOK_MOVE,
    LOOK_YOUNG,
    LOOK_FIGHT,
    LOOK_GIANT,
    LOOK_OLDEST,
    LOOK_LARGEST,
    LOOK_FIRST_KILL,
    LOOK_MORE_KILLS,
    LOOK_HUNTER_KILL,
    LOOK_REASON_COUNT
} LookReason;

/* The phrase for a reason, such as "first kill soon". */
const char *lookahead_reason_text(int reason);
/* What a life's coming season is worth watching, from its tally and age now
 * and later: kills and fights above slow things like growing (lookahead.c has
 * the weights). tired: reasons shown lately, bit r for reason r; those parts
 * count less, so the camera does not show the same kind of thing every time.
 * *why is set to the biggest part's LookReason. */
float lookahead_score(const LifeTally *now, uint32_t age_now,
                      const LifeTally *later, uint32_t age_later,
                      unsigned tired, int *why);
/* The living creature on screen with the best coming season (a life that
 * ends within it counts half), its LookReason in *reason, and in why a
 * line such as "Varo, a young hunter: first kill soon". LOOKAHEAD_NOBODY
 * (and an empty why) when the copy is not ready or nobody is alive. tired as
 * for lookahead_score: the window passes the reasons of its last picks. */
uint32_t lookahead_pick(const Lookahead *l, const Sim *shown, unsigned tired,
                        int *reason, char *why, size_t len);
/* Frees the copy; safe to call twice. The lead is kept. */
void lookahead_free(Lookahead *l);

#endif
