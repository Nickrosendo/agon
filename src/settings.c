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
        .veg_grow_chance = 0.25f,
        .veg_seed_chance = 0.0002f,
        .veg_start_chance = 0.03f,
        .veg_reach = 2,
        .veg_capacity = 50.0f,
        .veg_mature_age = 60,
        .veg_wither_chance = 0.02f,
        .veg_wither_ticks = 20,
    };
    return s;
}
