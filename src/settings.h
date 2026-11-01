/*
 * settings: every tunable number in one struct, so no logic hides a bare
 * constant. Later lessons add fields and a settings file parser.
 */
#ifndef AGON_SETTINGS_H
#define AGON_SETTINGS_H

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
     * probability fertility. Empty otherwise: sprouts with probability
     * veg_seed_chance * fertility. Planted: grows by 1 while
     * n <= veg_crowd_max, shrinks by 1 above it.
     *
     * Local carrying capacity: m is the total density of every cell within
     * veg_reach steps (the cell itself left out), and the cell's capacity
     * is fertility * veg_capacity. While m >= capacity an empty cell
     * cannot sprout and a planted cell cannot grow; above it, a planted
     * cell shrinks. Poor soil therefore holds sparse plants, rich soil
     * dense ones, and no area fills up completely. */
    int veg_birth_lo;
    int veg_birth_hi;
    int veg_crowd_max;
    float veg_seed_chance;
    float veg_start_chance; /* chance a soil cell starts planted */
    int veg_reach;          /* radius of the neighbourhood for m */
    float veg_capacity;     /* largest m a cell of fertility 1 allows */
} Settings;

Settings settings_default(void);

#endif
