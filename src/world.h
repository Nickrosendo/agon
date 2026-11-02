/*
 * world: the grid the simulation lives on. Each layer is one flat array
 * with one value per cell, row after row; cell (x, y) is at y * w + x.
 * The edges wrap around on both axes.
 */
#ifndef AGON_WORLD_H
#define AGON_WORLD_H

#include <stdbool.h>
#include <stdint.h>

enum { TERRAIN_WATER, TERRAIN_SOIL, TERRAIN_ROCK };
#define VEG_MAX 4

typedef struct {
    int w, h;
    uint8_t *terrain;
    float *fertility;        /* 0..1 */
    uint8_t *veg, *veg_next; /* 0..VEG_MAX */
    uint16_t *veg_age;       /* ticks since the plant on this cell sprouted */
    uint8_t *veg_wither;     /* 0 healthy, else ticks spent withering */
    uint8_t *veg_rule;       /* lesson 9: packed thresholds per cell */
    float *carrion;          /* energy lying on the cell */
    float *carrion_level;    /* food-chain level of that carrion */
    float *scent, *scent_next; /* smell of carrion, spreading from it */
    int32_t *occupant;       /* creature index, or -1 */
} World;

/* Allocates every layer: occupant set to -1, everything else to 0.
 * Returns false for a width or height below 1, a world too large to
 * index with an int, or when memory runs out; nothing is left allocated
 * then. On success the world must later be given to world_free. */
bool world_init(World *w, int width, int height);
/* Puts every layer back to its starting value (occupant -1, the rest 0)
 * without reallocating, so a new world can reuse the memory. */
void world_clear(World *w);
/* Frees every layer and sets the pointers to NULL; safe to call twice. */
void world_free(World *w);
int world_idx(const World *w, int x, int y); /* wraps both axes */

/*
 * A vegetation rule packed into one byte (veg_rule). Three small numbers
 * share it, each in its own group of bits:
 *
 *   bit   7     6 5 4        3 2           1 0
 *         unused crowd max   birth width   birth low - 1
 *
 * birth low is 1..4 (stored as 0..3), birth width is 0..3, so an empty
 * cell can sprout by the rule when it has from birth low to birth low +
 * width planted neighbours (birth high, 1..7). crowd max is 0..7: a
 * plant grows while it has at most that many planted neighbours.
 *
 * To read a group: shift it down to bit 0, then keep only its bits with
 * a mask (0x3 is binary 11, two bits; 0x7 is 111, three bits). To write
 * one: mask the value to its width and shift it up into place.
 *
 * A cell's rule means something only while the cell is planted; on bare
 * soil it is whatever the last plant there carried.
 */
#define VEG_RULE_LO_SHIFT 0
#define VEG_RULE_WIDTH_SHIFT 2
#define VEG_RULE_CROWD_SHIFT 4
#define VEG_RULE_LO_MASK 0x3u
#define VEG_RULE_WIDTH_MASK 0x3u
#define VEG_RULE_CROWD_MASK 0x7u
/* The seven bits in use; a mutation flips one of them. */
#define VEG_RULE_BITS 7
#define VEG_RULE_USED_MASK 0x7Fu

/* static inline in a header: every .c file that includes it gets its own
 * private copy, which the compiler can paste straight into the caller.
 * Without static, two files including the header would each define the
 * same function and the link would fail. */
/* (unsigned)rule first: in an expression, a uint8_t is promoted to a
 * signed int before anything else happens, and & with an unsigned mask
 * would then mix signed and unsigned (-Wconversion warns). */
static inline int veg_rule_birth_lo(uint8_t rule)
{
    return (int)(((unsigned)rule >> VEG_RULE_LO_SHIFT) & VEG_RULE_LO_MASK) +
           1;
}

static inline int veg_rule_birth_hi(uint8_t rule)
{
    return veg_rule_birth_lo(rule) +
           (int)(((unsigned)rule >> VEG_RULE_WIDTH_SHIFT) & VEG_RULE_WIDTH_MASK);
}

static inline int veg_rule_crowd_max(uint8_t rule)
{
    return (int)(((unsigned)rule >> VEG_RULE_CROWD_SHIFT) &
                 VEG_RULE_CROWD_MASK);
}

/* How aggressive a rule is, 0..1: 0 for the most modest rule (born only
 * with exactly 4 planted neighbours, grows only with none), 1 for the
 * most aggressive (born with 1 to 4, grows whatever the crowding). Each
 * of the three numbers counts by how far it is from modest: birth low
 * below 4 (0..3), width (0..3), crowd limit (0..7); 13 at most. The
 * starting rule (3-4 / 3) is 5 / 13, about 0.38. */
static inline float veg_rule_vigour(uint8_t rule)
{
    int lo = veg_rule_birth_lo(rule);
    int width = veg_rule_birth_hi(rule) - lo;
    int points = (4 - lo) + width + veg_rule_crowd_max(rule);
    return (float)points / 13.0f;
}

/* Clamps v into lo..hi. */
static inline int veg_rule_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* Packs a rule. Values that do not fit are clamped: birth_lo into 1..4,
 * birth_hi into birth_lo .. birth_lo + 3, crowd_max into 0..7. */
static inline uint8_t veg_rule_pack(int birth_lo, int birth_hi, int crowd_max)
{
    int lo = veg_rule_clamp(birth_lo, 1, 4);
    int width = veg_rule_clamp(birth_hi, lo, lo + 3) - lo;
    int crowd = veg_rule_clamp(crowd_max, 0, 7);
    /* Each part is small and non-negative, so converting it to unsigned
     * changes nothing; the shifts then work on unsigned values, and the
     * result (at most 0x7F) fits a uint8_t. */
    unsigned packed = ((unsigned)(lo - 1) << VEG_RULE_LO_SHIFT) |
                      ((unsigned)width << VEG_RULE_WIDTH_SHIFT) |
                      ((unsigned)crowd << VEG_RULE_CROWD_SHIFT);
    return (uint8_t)packed;
}

#endif
