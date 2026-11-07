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

    /* Climate. Rain is noise above rain_threshold, fading to a new pattern
     * every rain_period ticks. Each climate_every ticks, a soil cell gains
     * rain_gain * rain * (1 - water) and loses water_loss * water per tick.
     * Temperature is fixed noise spread over 0..1. A cell's region comes from
     * temperature and water, by the region_ limits (climate.h). */
    float shallow_band;     /* heights this far below water_level are shallow */
    float temperature_scale; /* cells between lattice points, temperature */
    float rain_scale;       /* the same, for the rain */
    float rain_threshold;   /* 0 rains everywhere, 1 nowhere */
    int rain_period;        /* ticks for one pattern to fade into the next */
    /* Wet and dry times: over each rain_cycle ticks the threshold moves
     * rain_swing below rain_threshold and rain_swing above it, starting wet. */
    int rain_cycle;
    float rain_swing;
    int climate_every;      /* ticks between updates of rain and water */
    float rain_gain;        /* water soil takes per tick in full rain */
    float water_loss;       /* share of its water soil loses per tick */
    /* Seasons in the weather. In summer the rain threshold is this much
     * higher, so less of the map gets rain. In winter the gain goes into a
     * snow layer instead of the soil, and the first tick of spring melts all
     * of it into the soil's water. */
    float summer_rain_rise;
    float region_cold_below; /* temperature below this is cold */
    float region_hot_above;  /* temperature above this can be hot and dry */
    float region_dry_below;  /* water below this, when hot, is hot and dry */
    float region_wet_above;  /* water above this is wet ground */

    /* Vegetation. n is how many of a soil cell's eight neighbours carry
     * plants. Every planted cell holds a living plant (world.h, Plant): its
     * stored energy, its fruit and its genes.
     *
     * An empty cell with planted neighbours picks one of them at random, its
     * parent. If that plant can seed (see below), the cell sprouts with
     * probability fertility * veg_sprout_rate. A plant comes only from a plant:
     * an empty cell with no planted neighbour never sprouts, so plants that die
     * out everywhere are gone. A planted cell grows by 1 with probability
     * veg_grow_chance, if it is well enough and can pay plant_grow_cost.
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
     * per tick; it also starts when its energy runs out. A withering plant no
     * longer grows or shrinks; after veg_wither_ticks ticks it disappears and
     * the cell is bare soil. */
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

    /* A plant's life. Its fit, 0..1, says how well the place suits it: the
     * water part is the soil's water over what it needs, at most 1, where it
     * needs plant_water_min + (1 - plant_water_min) * water_need; the
     * temperature part falls from 1 to 0 as the distance from temp_pref reaches
     * plant_temp_width. Fit is the two multiplied.
     *
     * Each tick it gains plant_gain * fit * vigour and pays plant_upkeep, where
     * vigour = plant_thrift + (1 - plant_thrift) * water_need: a thirsty plant
     * grows faster where there is water, and that is what thirst buys. Stored
     * energy stays within 0..plant_energy_max. */
    float plant_gain;
    float plant_upkeep;
    /* Seasons. In spring plant_gain and plant_fruit_rate are multiplied by
     * spring_plenty. A plant that is not growing (every plant in winter, a
     * fruit tree in autumn) gains nothing, grows, seeds and fruits not at all,
     * and pays plant_upkeep * plant_rest_upkeep. */
    float spring_plenty;
    float plant_rest_upkeep;
    /* Leaf trees. A leaf plant that fits (plant_seed_fit) gains tree_growth
     * of size on each spring and summer tick, up to tree_size_max. From
     * tree_shade_size on it shades its own cell and the eight around it. A
     * leaf tree can start withering only from tree_mature_age on, in place of
     * veg_mature_age, which stays for fruit trees. In summer a creature out of
     * shade pays summer_heat_upkeep per tick, flat, whatever its size. */
    float tree_growth;
    float tree_size_max;
    float tree_shade_size;
    int tree_mature_age;
    float summer_heat_upkeep;
    float plant_energy_max;
    float plant_start_energy; /* a starting plant's energy */
    float plant_thrift;
    float plant_water_min;
    float plant_temp_width;
    /* Seeds. A plant can seed, grow or fruit only at a fit of plant_seed_fit or
     * more. Seeding also needs plant_seed_energy stored, and costs the parent
     * plant_seed_cost. The sprout starts with plant_seed_start. Each gene of a
     * seed moves with chance plant_mutation by a normal step of
     * plant_mutation_spread. */
    float plant_seed_fit;
    float plant_seed_energy;
    float plant_seed_cost;
    float plant_seed_start;
    float plant_grow_cost;
    float plant_mutation;
    float plant_mutation_spread;
    /* Fruit. A plant offering fruit (world.h, plant_offers_fruit) and holding
     * plant_fruit_energy or more makes plant_fruit_rate fruit per tick, up to
     * plant_fruit_max,
     * paying plant_fruit_cost per fruit. Eating one gives bite_fruit_energy
     * and takes nothing else from the plant. The eater keeps one seed for
     * plant_gut_min to plant_gut_max ticks, by the seed's gut_time gene, and
     * drops it where it then stands; a seed landing on bare soil sprouts with
     * plant_dung_start, far more than a seed falling beside its parent. */
    float plant_fruit_thirst; /* water a fruit tree needs on top of its gene's */
    float plant_fruit_near; /* a fruit tree's seeds falling beside it, against a leaf plant's */
    float plant_fruit_energy;
    float plant_fruit_rate;
    float plant_fruit_cost;
    float plant_fruit_max;
    float bite_fruit_energy;
    int plant_gut_min;
    int plant_gut_max;
    float plant_dung_start;
    /* Leaves. Eating them gives bite_veg_energy per density step, and takes plant_leaf_harm of the plant's stored energy per step. No bite takes the last plant_leaf_root steps: grazed down to its root, a plant offers nothing and regrows. On since 2026-11-05, with fruit made cheaper (see settings.c); on alone, leaf plants took over. */
    int plant_leaf_root;     /* density steps of a leaf plant no bite can take */
    float plant_leaf_harm;

    /* Creatures. Energy pays for everything: every tick costs
     * upkeep_base, a move costs move_cost more (even a blocked one), and
     * a bite of vegetation gains bite_veg_energy. At zero energy a
     * creature loses health, and at zero health it dies. From mate_age ticks and mate_threshold * size energy
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
    /* Parents whose bodies are less alike than this (genome_body_likeness, body
     * trait genes only) have no child, from the first tick. 0 turns it off. The
     * default lets about 94 % of random founders' pairs mate. */
    float mate_barrier;
    int mate_window;         /* ticks a wish to mate lasts */
    int mate_age;            /* ticks before a creature can mate */
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
     * size at birth from size_min to birth_size_max, speed (move points gained per
     * tick) from speed_min to 1, sight from 1 to sight_max cells. A
     * creature may move only with at least 1 move point. Upkeep per
     * tick is upkeep_base + size_upkeep * size + speed_upkeep * speed +
     * sight_upkeep * sight + the digestion cost (below), and mating
     * needs mate_threshold * size.
     * Growth: energy above growth_surplus * mate_threshold * size is surplus. The
     * growth gene's share of it goes into body, at growth_cost energy per unit of
     * size, with no top size. */
    float size_min;
    float birth_size_max;
    float growth_surplus;
    float growth_cost;
    float speed_min;
    int sight_max;
    float size_upkeep;
    float speed_upkeep;
    float sight_upkeep;      /* per cell of sight */

    /* Food. A bite looks at the eight cells around the creature and eats
     * the food that gives it the most energy, and never hurts anyone
     * (see act_bite in creature.c). Energy gained
     * is what was taken times the efficiency for that food: digest_floor
     * + (1 - digest_floor) * gene, with GENE_LEAF, GENE_FRUIT or GENE_MEAT.
     * Digesting costs upkeep every tick: digest_upkeep * (leaf + fruit +
     * meat)^2. The square
     * makes a generalist (both genes 1) pay four times what a specialist
     * (one gene 1) pays, while a little of the second food costs a
     * specialist almost nothing at first, so a plant-eater can drift
     * towards meat one small step at a time. A carrion bite takes at most
     * bite_meat_energy. */
    float bite_meat_energy;
    float digest_floor;
    float digest_upkeep;
    /* Better at one food means worse at the others: the part of a gene's yield
     * above the floor is cut by digest_tradeoff times the strongest of the
     * other two genes. Never below digest_floor. */
    float digest_tradeoff;

    /* What regions ask of a body. A creature whose colour matches its region's
     * ground is seen from only (1 - camouflage) of the distance, falling off to
     * no effect at camouflage_width of the colour wheel away. Small bodies lose
     * heat: cold_upkeep / size per tick in a cold region. Big bodies overheat:
     * heat_upkeep * size per tick in a hot and dry one. A step onto marsh or sand
     * costs rough_step_cost * (1 - legs) extra, onto shallows the same times
     * (1 - swim). The price of those bodies: legs_step_cost * legs on any other
     * ground, swim_step_cost * swim on any land. */
    float camouflage;
    float camouflage_width;
    float cold_upkeep;
    float heat_upkeep;
    float rough_step_cost;
    float legs_step_cost;
    float swim_step_cost;

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
    /* A neighbour counts as a relative in the senses from this genome_kinship up. */
    float kin_sense;
    /* An attack does not strike a creature whose genome_kinship with the attacker
     * is this or more; above 1, nobody is spared. */
    float family_peace;
    /* Nor one closer than this on the colour wheel; 0, colour spares nobody. */
    float colour_peace;
    /* A newborn this far round the colour wheel from its family's colour starts a
     * family of its own; 0.5 or more, nobody ever does. */
    float family_split;
    /* The share of upkeep paid on a tick spent resting. */
    float rest_upkeep;
    /* The share paid on a tick of deep sleep (ACT_SLEEP), of the upkeep, ageing
     * included, and of the summer heat. */
    float sleep_upkeep;
    /* The fewest ticks a deep sleep lasts: a brain that asks to sleep for less
     * sleeps this long, unless a bite wakes it. Without it a brain could sleep
     * every other tick and pay a sleeper's upkeep while still getting about.
     * 0 turns it off. */
    int min_sleep_ticks;
    /* Crowding: crowd_upkeep energy per tick for each creature past the first
     * crowd_free within crowd_radius cells, whoever they are. 0 turns it off. */
    /* Territory: each tick an adult with a stranger on one of the four cells
     * beside it turns and attacks, whatever its brain chose, with chance
     * territory_drive * its territory gene. The gene costs territory_upkeep per
     * tick at full value. 0 turns the drive off. */
    float territory_drive;
    float territory_upkeep;
    float crowd_upkeep;
    int crowd_radius;
    int crowd_free;

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

    /* How many ticks make a year, in four seasons, spring first (years.h). A
     * rule: spring is the mating season, when every fed adult is willing with
     * no choice made, and later the weather and plants follow the seasons too.
     * Outside spring a child still needs both parents' brains to choose
     * ACT_MATE, which random brains almost never do. At the default speed of 10
     * ticks a second, the default makes a year a minute, 15 seconds a season. */
    int ticks_per_year;

    /* Life stories (chronicle.h): 1 records them, 0 records nothing. No rule
     * reads them, so turning this off changes no world, and a test checks it. */
    int chronicle;
    /* The body size from which a creature counts as a giant: crossing it is an
     * event in its story. 6 is where it is drawn three tiles wide. Only the story
     * reads it. */
    float giant_size;
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
