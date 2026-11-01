/*
 * settings: every tunable number in one struct, so no logic hides a bare
 * constant. Later lessons add fields and a settings file parser.
 */
#ifndef AGON_SETTINGS_H
#define AGON_SETTINGS_H

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
     * plants. Empty with veg_birth_lo <= n <= veg_birth_hi: sprouts with
     * probability fertility * veg_sprout_rate. Empty otherwise: sprouts
     * with probability veg_seed_chance * fertility. Planted: while
     * n <= veg_crowd_max, grows by 1 with probability veg_grow_chance;
     * above it, shrinks by 1.
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
    float veg_sprout_rate;  /* scales the chance to sprout beside plants */
    float veg_grow_chance;  /* chance per tick that a plant thickens */
    float veg_seed_chance;
    float veg_start_chance; /* chance a soil cell starts planted */
    int veg_reach;          /* radius of the neighbourhood for m */
    float veg_capacity;     /* largest m a cell of fertility 1 allows */
    int veg_mature_age;       /* ticks before a plant can start withering */
    float veg_wither_chance;  /* chance per tick, once mature */
    int veg_wither_ticks;     /* how long withering lasts */

    /* Creatures. Energy is the only currency: every tick costs
     * upkeep_base, a move costs move_cost more (even a blocked one), and
     * a bite of vegetation gains bite_veg_energy. At zero energy a
     * creature dies. From split_threshold on it can split, handing
     * split_share of its energy to the child. While fewer than
     * min_creatures are alive, new ones are placed at random (0 turns
     * this off). */
    int pool_capacity;       /* most creatures alive at once */
    int start_creatures;     /* placed when a world is built */
    int min_creatures;       /* the floor */
    char brain_name[BRAIN_NAME_MAX]; /* which brain every creature uses */
    float energy_start;      /* for creatures placed, not born */
    float upkeep_base;
    float move_cost;
    float bite_veg_energy;
    float split_threshold;
    float split_share;       /* 0..1 */
    float sense_age_half;    /* the age sensed as 0.5 */
    /* Old age: a creature dies on reaching max_age ticks, whatever its
     * energy (0 turns this off). Without it, a creature that sits and
     * eats the plant ahead can live for ever; once the pool is full
     * nobody is born, and selection stops. */
    int max_age;

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
} Settings;

Settings settings_default(void);

#endif
