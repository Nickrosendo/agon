#include "settings.h"

/* Returns the struct by value: the caller gets its own copy, as if every
 * field were assigned one by one. Fine for a small struct read once. */
Settings settings_default(void)
{
    Settings s = {
        .water_level = 0.38f,
        .rock_level = 0.80f,
        .noise_scale = 48.0f,
        .noise_octaves = 4,
        .fertility_scale = 24.0f,
        .veg_birth_lo = 3,
        .veg_birth_hi = 4,
        .veg_crowd_max = 3,
        .veg_sprout_rate = 0.4f,
        .veg_grow_chance = 0.05f,
        .veg_seed_chance = 0.0002f,
        .veg_start_chance = 0.03f,
        .veg_reach = 2,
        .veg_capacity = 50.0f,
        .veg_mature_age = 60,
        .veg_wither_chance = 0.02f,
        .veg_wither_ticks = 20,
        .pool_capacity = 4000,
        .start_creatures = 60,
        .min_creatures = 10,
        /* A char array inside a struct can be initialised from a string
         * literal: the characters are copied in and the rest is zero. */
        .brain_name = "random",
        .energy_start = 20.0f,
        .upkeep_base = 0.15f,
        .move_cost = 0.3f,
        .bite_veg_energy = 5.0f,
        .split_threshold = 30.0f,
        .split_share = 0.5f,
        .sense_age_half = 200.0f,
    };
    return s;
}
