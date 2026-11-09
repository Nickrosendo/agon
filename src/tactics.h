/*
 * tactics: notices tactics no rule wrote. It watches families (creatures
 * sharing a lineage) and counts facts about their fights and decisions: when
 * their kills happen, whether relatives struck the same victim, whether the
 * victim was alone, whether relatives struck back at an attacker, who of them
 * took the hits, when they call, whether they keep together in the dark. A
 * family's habit counts as a tactic when it is both uneven (far more often
 * than chance or the other families) and useful (the family does better than
 * the other families of its diet).
 *
 * Like the chronicle, nothing here is read by a rule, draws a random number
 * or allocates after tactics_init, so watching cannot change a world. Only this
 * module knows the tactics' names. It is not saved: after a load it starts
 * counting again.
 */
#ifndef AGON_TACTICS_H
#define AGON_TACTICS_H

#include <stdbool.h>
#include <stdint.h>

#include "brain.h"
#include "creature.h"
#include "settings.h"

struct Chronicle;

enum {
    TACTIC_NIGHT_HUNT, /* hunters: kills in the dark */
    TACTIC_PACK_HUNT,  /* hunters: a relative struck the same victim */
    TACTIC_STRAYS,     /* hunters: the victim had no relative near */
    TACTIC_DEFENCE,    /* plant-eaters: two or more struck an attacker back */
    TACTIC_SHIELD,     /* plant-eaters: the toughest of them took the hits */
    TACTIC_ALARM,      /* anyone: calls far more with a hunter in sight */
    TACTIC_HUDDLE,     /* plant-eaters: keep together in the dark */
    TACTIC_COUNT
};

/* How many families the watch follows at once. A world rarely has more than a
 * few dozen once its founders' families die out; while the table is full,
 * families without a row are not followed. */
#define TACTIC_FAMILIES 256
/* Light below this is dark: with light 0.5 + 0.5 cos, a third of every day. */
#define TACTIC_DARK 0.25f
/* The share of the dark in a day, for the night hunt's chance level. */
#define TACTIC_DARK_SHARE (1.0f / 3.0f)

/* One family's counts. Every count is a float that fades a little at each
 * check, so old events weigh less and the counts cover about the last year. */
typedef struct {
    bool used;
    bool hunters;      /* more than half its members were hunters at the last check */
    uint32_t lineage;
    int members;       /* alive at the last check */
    float member_ticks;
    /* Its hunters' kills: all, in the dark, with a relative's hit on the victim,
     * of a victim with no relative near. */
    float kills, kills_dark, kills_pack, kills_lone;
    /* Attacks on its members (a fight's first chosen hit), how many ended in a
     * death, how many of those deaths came in the dark, and how many attacks
     * were struck back by two or more members. */
    float attacks_on, deaths, deaths_dark, defended;
    /* Hits on a member with relatives near: how many landed on the toughest of
     * them, how many would by chance, and how many such hits. */
    float shield_toughest, shield_expected, shield_hits;
    /* Decisions with a hunter in sight and without, and calls at each. */
    float seen_decisions, seen_calls, quiet_decisions, quiet_calls;
    /* Decisions in the dark and in the light, and the kin-near sense summed. */
    float dark_decisions, dark_kin, light_decisions, light_kin;
    uint8_t confirmed; /* bit k: tactic k holds now */
    uint8_t told;      /* bit k: tactic k already went into its members' stories */
    float lift[TACTIC_COUNT]; /* at the last check */
} TacticFamily;

/* The index from a lineage to its row: an open-addressing table (tactics.c). */
#define TACTIC_INDEX_BITS 10
#define TACTIC_INDEX (1 << TACTIC_INDEX_BITS)

typedef struct TacticWatch {
    TacticFamily *rows; /* TACTIC_FAMILIES rows, allocated once */
    /* A row number, or -1. int16_t holds -32768..32767, enough for 256 rows. */
    int16_t index[TACTIC_INDEX];
    /* Bit k: tactic k was confirmed for some family since the last reset, and
     * the highest lift it reached. */
    uint8_t seen;
    float best_lift[TACTIC_COUNT];
    /* The share of plant-eaters with no relative within KIN_NEAR, measured at
     * the last check: the chance level for picking strays. */
    float lone_share;
} TacticWatch;

bool tactics_init(TacticWatch *t);
/* Forgets every family and what was seen. */
void tactics_clear(TacticWatch *t);
void tactics_free(TacticWatch *t);

/* A hit a brain chose to make (never the territory drive's), landed by by on
 * target, before the hit joins the target's ring. killed: the hit took the
 * target's health to 0. light: the Pool's light this tick. */
void tactics_note_hit(TacticWatch *t, const World *w, const Pool *p,
                      const Creature *by, const Creature *target, bool killed,
                      const Settings *s);
/* A decision: the senses it was made on and whether the creature called. */
void tactics_note_decision(TacticWatch *t, const Creature *c, const Senses *in,
                           bool calling, float light);
/* Every stats_every ticks: fades the counts, counts members, works out each
 * family's lifts and confirms its tactics. ch may be NULL. */
void tactics_check(TacticWatch *t, const World *w, Pool *p,
                   struct Chronicle *ch, const Settings *s);

/* Whether tactic was confirmed for any family since the last reset. */
bool tactics_seen(const TacticWatch *t, int tactic);
void tactics_reset_seen(TacticWatch *t);
/* The highest lift tactic reached when confirmed since the reset; 0 if never. */
float tactics_lift(const TacticWatch *t, int tactic);
/* Families now showing tactic, and in *of_diet the families of its diet. */
int tactics_families(const TacticWatch *t, int tactic, int *of_diet);
/* A short name: night, pack, strays, defence, shield, alarm, huddle. */
const char *tactics_name(int tactic);

#endif
