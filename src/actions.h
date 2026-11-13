/*
 * actions: what a player does to a world, as records in a log. Each record
 * names the tick it belongs to and is applied at the start of that tick,
 * before anything moves (the player agency design, section 4). A live click
 * and a replay from a world code go through the same log, which is what
 * makes a replay come out the same.
 *
 * The player changes bodies, the weather and the odds, never a decision:
 * nothing here touches a brain.
 */
#ifndef AGON_ACTIONS_H
#define AGON_ACTIONS_H

#include <stdbool.h>
#include <stdint.h>

/* Only a name: "a struct called Sim exists". The full definition is in sim.h,
 * which includes this header, so this one cannot include sim.h back. A pointer
 * to a struct whose insides are unknown here is allowed; using its fields is
 * not, and actions.c, which needs them, includes sim.h itself. */
struct Sim;

enum ActionKind {
    ACTION_TRAIT,   /* creature's gene set to value */
    ACTION_WEATHER, /* a weather event (weather.h) for one season */
    ACTION_KIND_COUNT
};

/*
 * One action. Every kind uses the same record and leaves the fields it does
 * not need at 0: a plain struct is simpler to save and to copy than one
 * shaped differently per kind. The widths are fixed (uint8_t, uint32_t) so
 * the save file holds the same bytes on every compiler.
 */
typedef struct {
    uint64_t tick;     /* applied at the start of this tick */
    uint8_t kind;      /* an ActionKind */
    uint8_t gene;      /* ACTION_TRAIT: which gene (genome.h) */
    uint8_t weather;   /* ACTION_WEATHER: which event */
    uint32_t creature; /* ACTION_TRAIT: the creature's id */
    float value;       /* ACTION_TRAIT: the gene's new value, 0..1 */
} PlayerAction;

/*
 * The log: count entries in items, in order of tick, room for capacity.
 * items is on the heap and grows as entries come; the Sim owns it. next is
 * the first entry not yet applied, so a tick looks at most at the entries
 * due now instead of the whole log.
 */
typedef struct {
    PlayerAction *items;
    int count, capacity;
    int next;
} ActionLog;

/* Appends a. False when memory runs out, or when a.tick is before the last
 * entry's: the log stays in order of tick, so replaying it is one pass. */
bool actions_add(ActionLog *log, PlayerAction a);
/* Empties the log, keeping its memory. */
void actions_clear(ActionLog *log);
void actions_free(ActionLog *log);
/* Points next at the first entry for tick or later: after a load, where the
 * file's tick says which entries have already happened. */
void actions_seek(ActionLog *log, uint64_t tick);

/*
 * The traits a player may give, as gene numbers (genome.h): size, armour,
 * weapon, swim, long legs, night eyes and learning. Only body genes: the sure
 * gene and the network weights shape decisions, and a tool never does.
 * "extern" says the array exists and is defined in actions.c; the count is a
 * separate constant because sizeof cannot see an array's length from here.
 */
extern const int ACTIONS_GIVABLE[];
enum { ACTIONS_GIVABLE_COUNT = 7 };

/* The word for a givable gene, for menus and summaries: "size", "armour",
 * "horn", "swimming", "long legs", "night eyes", "learning"; "?" for another. */
const char *actions_gene_name(int gene);

/* Records "give creature this gene at value", to happen at the start of the
 * next tick the Sim runs (s->tick, which has not run yet). False, and nothing
 * recorded, for a gene not in ACTIONS_GIVABLE, a value outside 0..1, no living
 * creature with that id, or no memory. */
bool actions_give_trait(struct Sim *s, uint32_t creature, int gene, float value);

/* Records a weather event (a WEATHER_ kind from weather.h, not WEATHER_NONE)
 * to start at the next tick and last one season, replacing any under way.
 * False for another kind or no memory. */
bool actions_weather(struct Sim *s, int kind);

/* Applies every entry due at s->tick, in the order they were added. An entry
 * whose creature has died (or never lived) does nothing; it stays in the log,
 * so a replay meets it and does nothing too. Called first thing by sim_step. */
void actions_apply_due(struct Sim *s);

/* A creature counts as carrying a given trait while its gene is this close
 * to the value given: mutation moves genes a little in every generation. */
#define ACTIONS_CARRIER_TOLERANCE 0.05f

/* Living creatures whose gene is within ACTIONS_CARRIER_TOLERANCE of value:
 * how many carry a given trait now. */
int actions_carriers(const struct Sim *s, int gene, float value);
/* Whether log entry i gives the same gene and value as an earlier entry, so
 * a trait given to several creatures is told of once. */
bool actions_gift_repeats(const ActionLog *log, int i);

#endif
