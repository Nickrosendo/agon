/*
 * settings: every tunable number in one struct, so no logic hides a bare
 * constant, and the settings file that can change them. One table in
 * settings.c lists every field; the parser, the range checks, the printed
 * list and the save file all walk it.
 */
#ifndef AGON_SETTINGS_H
#define AGON_SETTINGS_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdio.h>  /* FILE */

/* Room for a brain's name, the terminating '\0' included. */
#define BRAIN_NAME_MAX 32

typedef struct {
    /* Terrain. Heights are spread over 0..1 for each world; below
     * water_level is water, at or above rock_level is rock, soil between. */
    float water_level;
    float rock_level;
    float noise_scale;   /* cells between lattice points, first layer */
    int noise_octaves;   /* layers of noise, each twice as fine */
    float fertility_scale; /* the same, for the fertility field */

    /* Vegetation. n is how many of a soil cell's eight neighbours carry
     * plants. Every planted cell carries its own rule (world.h, veg_rule):
     * a birth range and a crowd limit.
     *
     * An empty cell with planted neighbours picks one of them at random,
     * its parent. If n is inside the parent's birth range, the cell
     * sprouts with probability fertility * veg_sprout_rate; otherwise it
     * stays bare. A sprout copies its parent's rule, and with probability
     * veg_mutation one of the rule's seven bits is flipped. A plant comes
     * only from a plant: an empty cell with no planted neighbour never
     * sprouts, so plants that die out everywhere are gone. A planted cell,
     * while n is at most its own crowd limit, grows by 1 with probability
     * veg_grow_chance; above it, shrinks by 1.
     *
     * veg_birth_lo, veg_birth_hi and veg_crowd_max are the rule the
     * starting plants carry, clamped to what a rule byte can hold
     * (birth_lo 1..4, birth_hi up to birth_lo + 3, crowd_max 0..7).
     *
     * Local carrying capacity: m is the total density of every cell within
     * veg_reach steps (the cell itself left out), and the cell's capacity
     * is fertility * veg_capacity. While m >= capacity an empty cell
     * cannot sprout and a planted cell cannot grow; above it, a planted
     * cell shrinks. Poor soil therefore holds sparse plants, rich soil
     * dense ones, and no area fills up completely.
     *
     * Life cycle: every tick a plant ages by one. From veg_mature_age on,
     * a healthy plant starts withering with probability veg_wither_chance
     * per tick. A withering plant no longer grows or shrinks; after
     * veg_wither_ticks ticks it disappears and the cell is bare soil. */
    int veg_birth_lo;
    int veg_birth_hi;
    int veg_crowd_max;
    float veg_mutation;     /* chance a sprout's rule has one bit flipped */
    /* Costs of an aggressive rule, by its vigour (0..1, see
     * veg_rule_vigour in world.h). A bite of the plant gives
     * bite_veg_energy * (1 - veg_vigour_food * vigour): fast growers are
     * less nourishing. The plant may start withering from
     * veg_mature_age * (1 - veg_vigour_ageing * vigour), at least 1:
     * fast growers die young. 0 turns either cost off. */
    float veg_vigour_food;
    float veg_vigour_ageing;
    float veg_sprout_rate;  /* scales the chance to sprout beside plants */
    float veg_grow_chance;  /* chance per tick that a plant thickens */
    float veg_start_chance; /* chance a soil cell starts planted */
    int veg_reach;          /* radius of the neighbourhood for m */
    float veg_capacity;     /* largest m a cell of fertility 1 allows */
    int veg_mature_age;       /* ticks before a plant can start withering */
    float veg_wither_chance;  /* chance per tick, once mature */
    int veg_wither_ticks;     /* how long withering lasts */
    /* A cap on the planted area: while at least veg_max_cells cells carry
     * plants, no empty cell sprouts. Counted once at the start of each
     * tick, so one tick of sprouting can overshoot it. 0: no cap. */
    int veg_max_cells;

    /* Creatures. Energy is the only currency: every tick costs
     * upkeep_base, a move costs move_cost more (even a blocked one), and
     * a bite of vegetation gains bite_veg_energy. At zero energy a
     * creature dies. From mate_age ticks and mate_threshold * size energy
     * it can wish to mate, for mate_window ticks; a child is born when two
     * neighbours wish together, each handing mate_share of its energy to
     * it. While fewer than min_creatures are alive, new ones are placed at
     * random; 0, the default, means nobody appears after the first tick. */
    int pool_capacity;       /* most creatures alive at once */
    /* Founders, placed when a world is built, as a share of its soil cells
     * and at most pool_capacity. A share and not a count, so that a small
     * world is not swamped and a large one not left empty. */
    float start_share;
    /* The floor. Off by default, and kept so that runs with and without it
     * can be compared: with it on, a world can never die. */
    int min_creatures;
    char brain_name[BRAIN_NAME_MAX]; /* which brain every creature uses */
    float energy_start;      /* for creatures placed, not born */
    float upkeep_base;
    float move_cost;
    float bite_veg_energy;
    float mate_threshold;
    float mate_share;        /* 0..1 */
    int mate_window;         /* ticks a wish to mate lasts */
    int mate_age;            /* ticks before a creature can mate */
    /* The mating season: for the first season_ticks of every season_every
     * ticks, every fed adult is willing with no choice made. Outside it, a
     * child still needs both parents' brains to choose ACT_MATE. Random brains
     * almost never do, so a world with no season stops breeding: see the
     * tables in the plan. season_ticks 0: no season. */
    int season_every;
    int season_ticks;
    float sense_age_half;    /* the age sensed as 0.5 */
    /* Ageing, a cost in place of an age limit. Upkeep gains ageing_upkeep * e *
     * e + slow_ageing_upkeep * (1 - gene), where e = age * rate / ageing_span
     * and rate = ageing_min_rate + (1 - ageing_min_rate) * gene, for GENE_AGEING.
     * A gene of 1 reaches e = 1 at ageing_span ticks; a gene of 0 ages at
     * ageing_min_rate of that speed, but pays more all its life. */
    float ageing_upkeep;
    float ageing_span;       /* above 0: it is divided by */
    float ageing_min_rate;
    float slow_ageing_upkeep;
    /* A fixed death age: a creature dies on reaching max_age ticks, whatever
     * its energy. 0, the default, means no such age. Kept so that runs with
     * and without it can be compared. */
    int max_age;
    /* A plant-eater cannot mate while max_plant_eaters plant-eaters are
     * alive (see pool_count_plant_eaters). Hunters are limited only by
     * the pool's capacity. 0, the default: no cap. Kept for comparisons. */
    int max_plant_eaters;
    /* How max_plant_eaters tells a plant-eater: 1 by its food-chain level
     * (below LEVEL_MEAT, as the panel counts), 0 by its genes (plant
     * digestion gene above the meat one). By genes, a creature with
     * hunter genes that lives on plants is never capped. An int, not a bool,
     * so the settings file can read it like every other whole number. */
    int cap_by_level;

    /* Inheritance. A child's genome is its parent's with each value, with
     * probability mutation_rate, moved by a normal step whose spread is
     * mutation_spread times that value's range (1 for a trait, twice
     * weight_limit for a weight). Weights stay within plus or minus
     * weight_limit; a new random genome draws them with spread
     * weight_start_spread. */
    float mutation_rate;
    float mutation_spread;
    float weight_limit;
    float weight_start_spread;

    /* Traits. Each gene is 0..1; the body uses it as a physical value:
     * size from size_min to size_max, speed (move points gained per
     * tick) from speed_min to 1, sight from 1 to sight_max cells. A
     * creature may move only with at least 1 move point. Upkeep per
     * tick is upkeep_base + size_upkeep * size + speed_upkeep * speed +
     * sight_upkeep * sight + the digestion cost (below), and mating
     * needs mate_threshold * size. */
    float size_min;
    float size_max;
    float speed_min;
    int sight_max;
    float size_upkeep;
    float speed_upkeep;
    float sight_upkeep;      /* per cell of sight */

    /* Food. A bite looks at the eight cells around the creature and eats
     * the food that gives it the most energy; a hunter (meat gene above
     * plant gene) with no carrion around attacks a neighbouring
     * plant-eater instead (see act_bite in creature.c). Energy gained
     * is what was taken times the efficiency for that food: digest_floor
     * + (1 - digest_floor) * gene, with GENE_PLANT or GENE_MEAT.
     * Digesting costs upkeep every tick: digest_upkeep * (plant +
     * meat)^2. The square
     * makes a generalist (both genes 1) pay four times what a specialist
     * (one gene 1) pays, while a little of the second food costs a
     * specialist almost nothing at first, so a plant-eater can drift
     * towards meat one small step at a time. A carrion bite takes at most
     * bite_meat_energy. */
    float bite_meat_energy;
    float digest_floor;
    float digest_upkeep;

    /* Attack. Every attack costs attack_cost. It hits with chance
     * a^k / (a^k + t^k), for attacker size a, target size t and k =
     * attack_steepness, better for each other creature that hit the target
     * within gang_window ticks (see attack_odds); a hit takes bite_damage * a
     * from the target's health. The attacker gains nothing directly: the kill
     * becomes carrion. */
    float attack_steepness;
    float attack_cost;
    float gang_bonus;
    int gang_window;
    /* The genes of a fight. A hit is multiplied by 1 + weapon_damage * weapon
     * gene, then by 1 - armour_block * armour gene, never below 0. Each gene at
     * full value costs its upkeep every tick. */
    float armour_block;
    float weapon_damage;
    float armour_upkeep;
    float weapon_upkeep;
    /* Health. A body holds health_per_size * size. Fed creatures heal heal_rate
     * per tick at heal_cost energy per point; at 0 energy a creature loses
     * starve_damage per tick instead. Death comes only at 0 health. */
    float health_per_size;
    float heal_rate;
    float heal_cost;
    float starve_damage;
    /* What a bite that finds nothing costs. Without this, waiting beside bare
     * ground costs less than walking to food a few cells away. */
    float bite_empty_cost;
    float bite_damage;

    /* Carrion. A death leaves body_energy * size plus any energy left.
     * Each tick carrion loses carrion_decay of itself; carrion_to_fertility
     * of what is lost feeds the cell's fertility, up to fertility_max (0:
     * carrion only rots away, and fertility never changes). */
    float body_energy;
    float carrion_decay;
    float carrion_to_fertility;
    float fertility_max;

    /* Scent. Each tick a cell's scent becomes (1 - scent_decay) times
     * [(1 - scent_spread) * its own + scent_spread * the average of its
     * four neighbours'], plus scent_emit * its carrion. So scent is
     * strongest on the carrion and fades with distance, over roughly
     * sqrt(scent_spread / scent_decay) cells; a creature can follow it
     * from far beyond its sight. */
    float scent_emit;
    float scent_spread;
    float scent_decay;

    /* Statistics: a sample every stats_every ticks (0: only when a world
     * is built). The panel's graphs show the last HISTORY samples (see
     * stats.h), so they span HISTORY * stats_every ticks. */
    int stats_every;

    /* How many ticks people read as a year. Display code alone reads this, so it
     * cannot change how a world evolves. At the default speed of 10 ticks a
     * second, the default makes a year 10 seconds. */
    int ticks_per_year;
} Settings;

Settings settings_default(void);

/* The longest line a settings file may have, its newline and the
 * terminating '\0' included. A longer line is an error. */
#define SETTINGS_LINE_MAX 256

typedef enum { SETTING_INT, SETTING_FLOAT, SETTING_TEXT } SettingType;

/* One row of the table: a field of Settings, described as data. offset is
 * where the field sits, in bytes from the start of the struct. min and max
 * are doubles because a double holds every int and every float exactly. */
typedef struct {
    const char *name;
    SettingType type;
    size_t offset;
    double min, max; /* inclusive; unused for SETTING_TEXT */
    const char *help;
} SettingInfo;

/* The table, row by row. i runs from 0 to settings_count() - 1. */
int settings_count(void);
const SettingInfo *settings_info(int i);
/* Where one setting lives inside *s, as a pointer of the right type. The
 * caller must use the one that matches info->type. */
int *settings_int(Settings *s, const SettingInfo *info);
float *settings_float(Settings *s, const SettingInfo *info);
char *settings_text(Settings *s, const SettingInfo *info);

/* Changes *s by a settings file: lines of "name = value", # comments,
 * blank lines, and [section] lines, which are accepted and ignored. If a
 * name appears twice the later line wins. False on any mistake, with a
 * one-line message (no newline) in err, naming the line; *s is then
 * unchanged, whatever lines came before the mistake. */
bool settings_load(Settings *s, const char *path, char *err, size_t errlen);

/* True when every field is inside its range and the brain's name is a
 * proper string. False with a message otherwise. */
bool settings_check(const Settings *s, char *err, size_t errlen);

/* Writes every setting as a commented "name = value" line: a settings
 * file that gives back *s when loaded. */
void settings_print(FILE *out, const Settings *s);

#endif
