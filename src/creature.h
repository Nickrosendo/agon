/*
 * creature: the pool that holds every creature, and what one creature
 * senses and does in a tick. Never touches the terminal and never calls
 * a brain: the simulation asks the brain and passes the answer here.
 */
#ifndef AGON_CREATURE_H
#define AGON_CREATURE_H

#include <stdbool.h>
#include <stdint.h>

#include "brain.h"
#include "genome.h"
#include "rng.h"
#include "settings.h"
#include "world.h"

/* Declares that a struct of this name exists, without saying what is in it, so
 * that a Pool can hold a pointer to one. chronicle.h defines it, and includes
 * this header; including that one here would make each header need the other
 * first. A pointer to a struct whose insides are unknown is allowed: every
 * object pointer has the same size. Reading a field through it is not. */
struct Chronicle;

/* Headings in clockwise order, so turning right is + 1 and turning left
 * is + 3 (that is, - 1), both modulo 4. */
enum { HEADING_NORTH, HEADING_EAST, HEADING_SOUTH, HEADING_WEST,
       HEADING_COUNT };

/* One hit a creature took: who landed it and when. tick is the low 32 bits of
 * the Sim's tick. Comparing two by unsigned subtraction stays right across the
 * wrap at 2^32, which is the one thing done with them. */
typedef struct {
    uint32_t by;
    uint32_t tick;
} Hit;

/* How many hits a creature remembers. At most four creatures can have it dead
 * ahead at once, since headings go four ways, so eight leaves room for repeats. */
#define HITS_KEPT 8

/* What ended a life. A typedef of an enum: the name reads as a type in a
 * parameter list, though C stores and accepts it as a plain integer, with no
 * check that a value is one of these. */
typedef enum {
    DEATH_WOUNDS, /* health taken to 0 by an attack */
    DEATH_HUNGER, /* health taken to 0 by having no energy */
    DEATH_AGE,    /* reached max_age, a limit that is off by default */
    DEATH_COUNT
} DeathCause;

/*
 * One thing that happened to a creature, for its life story. tick is the low 32
 * bits of the Sim's tick, as for a Hit. kind is a LIFE_ value (chronicle.h).
 * other_id is the other creature involved, when there is one, and value a number
 * that goes with the kind (a kill's count, a region, health left). Never read by
 * any rule: the world runs the same without it.
 *
 * Laid out as written, the struct takes 16 bytes, not 13: the compiler puts 3
 * unused bytes after kind so that other_id starts at a multiple of 4.
 */
typedef struct {
    uint32_t tick;
    uint8_t kind;
    uint32_t other_id;
    int32_t value;
} LifeEvent;

/* How many events a creature's own ring keeps: the newest few, for the panel.
 * The chronicle's table (chronicle.h) keeps longer stories for notable lives. */
#define CREATURE_EVENTS 8

/* LifeTally.flags, TALLY_ bits: things that happen once in a life, as single bits of one
 * byte. Bit k is the number 1 << k, so each flag has a bit of its own. */
enum {
    TALLY_FOUNDER = 1 << 0,     /* placed at the start, with no parents */
    TALLY_WAS_LARGEST = 1 << 1, /* has been the largest creature alive */
    TALLY_WAS_OLDEST = 1 << 2,  /* has been the oldest creature alive */
    TALLY_GIANT = 1 << 3,      /* has reached giant_size */
    TALLY_HOME_KNOWN = 1 << 4   /* home_region has been read from the ground */
};

/*
 * The counts a life story needs, kept up by the chronicle as events are noted.
 * Like the events, never read by a rule. Counts are uint32_t: a uint16_t would
 * save a few bytes per creature and wrap at 65,535, which a long life could reach.
 */
typedef struct {
    uint32_t parents[2]; /* ids; meaningless for a founder */
    uint32_t born;       /* the low 32 bits of the tick it was born on */
    uint32_t kills, hunter_kills, children, fights, heavy_wounds, kin_lost,
        moves;
    float max_size;
    uint8_t flags;       /* TALLY_ bits */
    /* Regions, as REGION_ values (climate.h), which include -1, so a signed
     * type. home_region is where it last settled; region_here where it stands,
     * for region_ticks ticks so far. */
    int8_t home_region, region_here;
    uint16_t region_ticks;
} LifeTally;

typedef struct {
    bool alive;
    int32_t next_free;  /* while dead: the next free slot, or -1 */
    uint32_t id;        /* unique for the whole run, never reused */
    /* The family name: a founder's own id, and for a child the name of one
     * parent, chosen at random, like a surname. Not a measure of relatedness:
     * with two parents, most of a child's genes can come from the other side. */
    uint32_t lineage;
    /* The colour its family started with. A child whose own colour has drifted
     * further than family_split from this leaves, and starts a family of its own. */
    float family_hue;
    int x, y;
    uint8_t heading;    /* a HEADING_ value */
    float energy;
    /* Death comes only at 0 health. Hits take it, healing restores it up to
     * creature_max_health, and having no energy drains it. */
    float health;
    /* Size gained since birth, on top of the size the genes gave at birth. See
     * creature_size. There is no top to it: upkeep and food limit it. */
    float grown;
    /* Hits taken in this whole life. The newest is at
     * recent_hits[(hits_taken - 1) % HITS_KEPT], and only the first hits_taken
     * entries mean anything: a fresh ring is all zeroes, which would otherwise
     * read as a hit by creature 0 on tick 0. */
    uint32_t hits_taken;
    Hit recent_hits[HITS_KEPT];
    float memory[MEMORY_VALUES]; /* what its brain last chose to keep */
    uint32_t age;       /* ticks lived */
    Action current;     /* what the brain last said */
    uint16_t hold_left; /* ticks of current still to do */
    float move_points;  /* a move needs 1; gains speed per tick, up to 1 */
    uint16_t mate_wish; /* ticks left of wanting to mate; 0 when it does not */
    /* Food-chain level: 1 + the energy-weighted average level of what it
     * has eaten (plants are level 1, so a pure plant-eater is at 2). eaten
     * is the energy those meals gave, the weight of the average. Until
     * the first meal, level is LEVEL_START or the mean of its parents'. */
    float level;
    float eaten;
    /* A seed from fruit it ate, and the ticks until it drops. While gut_ticks
     * is 0 there is none and gut_seed means nothing. Dying loses it: the slot's
     * next owner starts from zeroes. */
    Plant gut_seed;
    uint16_t gut_ticks;
    /* Stored inside the creature, not behind a pointer: GENOME_LEN floats
     * (916 bytes) in every slot, allocated with the pool and never freed
     * on its own. */
    Genome genome;
    /* Its life story: counts, and a ring of the newest events, newest at
     * events[(events_noted - 1) % CREATURE_EVENTS], as for recent_hits. Not part
     * of the fingerprint (see hash_creature in sim.c): recording must not change
     * the world, and a test checks that by comparing fingerprints. */
    LifeTally tally;
    uint32_t events_noted;
    LifeEvent events[CREATURE_EVENTS];
} Creature;

/* The level of a creature that has eaten nothing yet and has no parent:
 * counted as a plant-eater. */
#define LEVEL_START 2.0f
/* From this level on, more than half of a creature's food was meat: a
 * meat-eater. The panel's bands and the plant-eater cap (with
 * cap_by_level) both use it. */
#define LEVEL_MEAT 2.5f

/* Below 0 and above 1 are not possible, but a sum of 0.25f four times is
 * exactly 1 while a sum of 0.1f ten times is 0.99999994. A move is
 * allowed from 1 - MOVE_SLACK move points, so rounding cannot cost a
 * creature a whole tick. */
#define MOVE_SLACK 1e-4f

/*
 * Every creature lives in one array of capacity slots, allocated once.
 * A dead slot is free for the next birth. The free slots form a linked
 * list threaded through the array itself: free_head is the first free
 * slot, its next_free the second, and so on, ending at -1. Finding a free
 * slot is then one step, not a search.
 */
typedef struct {
    Creature *items;
    int capacity, count; /* count: how many are alive */
    int32_t free_head;
    uint32_t next_id;
    /* Plant-eaters alive as counted by pool_count_plant_eaters, plus
     * those born since. Deaths are not taken off until the next count. */
    int plant_eaters;
    /* Births that could not happen because every slot was taken. The pool is
     * a safety limit and not a rule of the world, so this should stay 0: it
     * is counted so that reaching the limit is never quiet. */
    uint32_t births_refused;
    /* Deaths so far, by cause. An array with an enum as its index: the enum's
     * values are plain numbers from 0, and DEATH_COUNT is how many there are. */
    uint32_t deaths[DEATH_COUNT];
    /* Whether this tick is in the mating season (spring), which season it is
     * (a SEASON_ value, years.h) and how far through its year (0 up to 1).
     * Worked out from the tick by the Sim before anyone acts, so they are
     * neither saved nor hashed: the tick is both. */
    bool in_season;
    uint8_t season;
    float year_phase;
    /* The low 32 bits of the current tick, set the same way and for the same
     * reason: the rules for hits need the date, and creature_act has no Sim. */
    uint32_t now;
    /* Where to record life stories, or NULL to record nothing. Set by the Sim
     * before anyone acts, like now, so it never points into a Sim that has moved.
     * Neither saved nor hashed. */
    struct Chronicle *chronicle;
} Pool;

/* Allocates capacity empty slots. False for a capacity below 1 or when
 * memory runs out; nothing to free then. */
bool pool_init(Pool *p, int capacity);
/* Empties every slot and starts ids from 0 again, without reallocating.
 * Does not touch any world: clear the world's occupant layer too. */
void pool_clear(Pool *p);
/* Counts the living plant-eaters (creature_counts_as_plant_eater) into
 * p->plant_eaters. The simulation calls it once per tick, before anyone
 * acts; a birth checks the count against max_plant_eaters and adds its
 * child to it. */
void pool_count_plant_eaters(Pool *p, const Settings *s);
/* Whether c counts towards max_plant_eaters: by its food-chain level
 * when s->cap_by_level is set, else by its genes (not a hunter). */
bool creature_counts_as_plant_eater(const Creature *c, const Settings *s);
/* Frees the slots and empties *p; safe to call twice. */
void pool_free(Pool *p);
/* Places a new creature at (x, y), heading north, with energy 0, age 0,
 * health 1 and an all-zero genome. Returns its slot index, or -1 when the pool
 * is full or the cell is not free soil; nothing changes then. The caller sets
 * the energy and the genome, and then the health, which depends on the body:
 * it starts at 1 only because a living creature must never hold 0. */
int pool_spawn(Pool *p, World *w, int x, int y);
/* Removes the creature in slot idx from the world, leaves its body as
 * carrion (body_energy * size plus any energy left, at its level), counts the
 * death under cause and frees the slot. Does nothing to a slot that is already
 * free. */
void pool_kill(Pool *p, World *w, int idx, const Settings *s, DeathCause cause);

/* Fills *out with what creature idx senses: its energy, age, size, meat
 * digestion gene, whether it is the mating season and whether the way ahead
 * is blocked; its hunger, its wounds, the relatives and strangers around it, and
 * its memory; and along each of its three rays (ahead, left,
 * right) the vegetation and the scent of carrion in the first cell, the
 * closeness of the nearest plant within sight, and
 * the nearest creature within sight: its closeness, its size relative to
 * the viewer, its kinship, whether it is a hunter, whether it wishes to mate
 * and how alike its body looks. A ray stops at rock. */
void creature_sense(const World *w, const Pool *p, int idx,
                    const Settings *s, Senses *out);
/* What a bite by creature idx would do right now. Reads only, and uses no
 * random numbers, so the drawing side can ask too and show the same thing the
 * simulation does. cell is the food's cell (world_idx), -1 for BITE_NOTHING. */
enum { BITE_NOTHING, BITE_CARRION, BITE_PLANT };
typedef struct {
    int kind; /* a BITE_ value */
    int cell;
    bool fruit; /* for BITE_PLANT: its fruit rather than its leaves */
} BiteChoice;
BiteChoice creature_bite_choice(const World *w, const Pool *p, int idx,
                                const Settings *s);
/* The slot of the creature an ACT_ATTACK by creature idx would strike: whoever
 * stands on the cell ahead, unless an earlier hit this tick has already taken it
 * to 0, or it is a close relative (genome_kinship at family_peace or above), or
 * of a similar colour (closer than colour_peace on the colour wheel). -1 for
 * nobody. Reads only, for the drawing side too. */
int creature_attack_target(const World *w, const Pool *p, int idx,
                           const Settings *s);
/* Carries out one tick of action a for creature idx. Returns the slot of
 * a child born from it, or -1 when none was.
 *
 * ACT_MATE: an adult (see Settings: mate_age, and mate_threshold by size)
 * starts wishing to mate for mate_window ticks. If an adult beside it is
 * wishing too, and a cell beside either is free soil, a child is born there:
 * each parent gives mate_share of its energy, and the child's genome is
 * genome_cross of the two.
 *
 * In the mating season (p->in_season) every adult tries this first, whatever
 * its action: a birth takes up its tick; otherwise it goes on to act as usual.
 * Out of season only ACT_MATE tries. */
int creature_act(World *w, Pool *p, int idx, Action a, const Settings *s,
                 Rng *r);
/* Starts a family: its own id is the name, and its own colour the family's. */
void creature_start_family(Creature *c);
/* Charges one tick of upkeep (rest_upkeep's share of it while resting) and ages the creature by one tick. With energy
 * left it heals, paying for it, and then turns the growth gene's share of any
 * surplus into body (growth_surplus, growth_cost); with none, it stays at exactly 0 and loses
 * starve_damage health instead. Returns false when its health is gone or it has
 * reached max_age: it must then be killed. One an attack already took to 0 stays
 * there: nothing is charged, and it does not heal. */
bool creature_upkeep(Creature *c, const Settings *s);

/* The body (see Settings): size is what the genes gave at birth, from size_min
 * to birth_size_max, plus what has grown since; speed in move points per tick
 * and sight in cells come from the genes alone. */
float creature_size(const Creature *c, const Settings *s);
/* How many density steps of a plant one bite takes at this size: the size to the
 * nearest whole number, at least 1, and at most a whole plant. */
int bite_steps(float size);
float creature_speed(const Creature *c, const Settings *s);
int creature_sight(const Creature *c, const Settings *s);
/* The health of an unhurt body. */
float creature_max_health(const Creature *c, const Settings *s);
/* How many creatures stand within radius cells of c, not counting c, of any kind
 * and any family. A square, so radius 1 is the 8 cells around. */
int creature_crowd(const World *w, const Creature *c, int radius);
/* Energy this creature's crowd costs it per tick: crowd_upkeep for each neighbour
 * past the first crowd_free. Being busy or resting makes no difference to it. */
float creature_crowd_cost(const World *w, const Creature *c, const Settings *s);
/* Energy one tick of life costs this creature, before any crowding. */
float creature_upkeep_cost(const Creature *c, const Settings *s);
/* Energy the region under it costs per tick, by the size of its body: the cold
 * spares large bodies, heat and dry ground spare small ones. */
float creature_region_cost(const World *w, const Creature *c, const Settings *s);
/* Whether a living leaf tree grown to shading size (plant_gives_shade) stands
 * on the creature's cell or any of the eight around it. */
bool creature_in_shade(const World *w, const Creature *c, const Settings *s);
/* Energy the summer sun costs per tick: summer_heat_upkeep in summer out of
 * shade, whatever the size of the body, else 0. season is a SEASON_ value. */
float creature_heat_cost(const World *w, const Creature *c, const Settings *s,
                         int season);
/* The energy a step onto this cell costs on top of move_cost, by its legs and
 * swimming body. Paid only for a step really taken. */
float creature_step_extra(const World *w, const Creature *c, int cell,
                          const Settings *s);
/* The share, 0 to 1, of a looker's sight at which this creature, standing on
 * seen_cell, can be seen: below 1 when its colour matches the ground there. */
float sight_reach(const Creature *seen, int seen_cell, const World *w,
                  const Settings *s);

/* True for a creature that digests meat better than plants. Says nothing
 * about who may attack whom: anyone may attack anyone. */
bool creature_is_hunter(const Creature *c);

/* Chance that an attacker of size a hits a target of size t:
 * a^k / (a^k + t^k). Equal sizes give 0.5; larger k makes size matter
 * more. A pure function (no state, no randomness), so tests can check it
 * exactly. */
float attack_chance(float a, float t, float k);
/* The same chance when others_recently other creatures have just hit the same
 * target: each adds gang_bonus to the attacker's weight, so
 * (1 + others * bonus) / (1 + others * bonus + (t/a)^k). Equal to attack_chance
 * at 0 others. It climbs towards 1 and only rounding can reach it, when the
 * sizes are enormously far apart. Asks nothing about who is related to whom. */
float attack_odds(float a, float t, float k, int others_recently,
                  float gang_bonus);
/* How many different creatures other than attacker have hit target within the
 * last window ticks. A hit exactly window ticks ago still counts. */
int creature_others_recently(const Creature *target, uint32_t attacker,
                             uint32_t now, int window);

/* Records a meal that gave energy from a source at source_level (1 for
 * plants, the carrion's level for meat): updates level and eaten. Does
 * nothing for energy <= 0. Does not add the energy itself. */
void creature_ate(Creature *c, float energy, float source_level);

#endif
