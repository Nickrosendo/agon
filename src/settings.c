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
        .start_creatures = 150,
        .min_creatures = 150,
        /* A char array inside a struct can be initialised from a string
         * literal: the characters are copied in and the rest is zero. */
        .brain_name = "net",
        .energy_start = 20.0f,
        .upkeep_base = 0.1f,
        .move_cost = 0.3f,
        .bite_veg_energy = 5.0f,
        .split_threshold = 30.0f,
        .split_share = 0.5f,
        .sense_age_half = 200.0f,
        .max_age = 1500,
        .mutation_rate = 0.05f,
        .mutation_spread = 0.05f,
        .weight_limit = 4.0f,
        .weight_start_spread = 1.0f,
        .size_min = 0.5f,
        .size_max = 1.5f,
        .speed_min = 0.25f,
        .sight_max = 6,
        .size_upkeep = 0.1f,
        .speed_upkeep = 0.1f,
        .sight_upkeep = 0.01f,
        .bite_meat_energy = 8.0f,
        .digest_floor = 0.25f,
        .digest_upkeep = 0.05f,
        .attack_steepness = 3.0f,
        .attack_cost = 0.5f,
        .bite_damage = 6.0f,
        /* Off: in a quick check (five seeds, 20,000 ticks) hunters died out
         * on 3 of 5 seeds with it on at kinship 0.5, on 2 of 5 at 0.8 and
         * at 0.95, and on none with it off. */
        .pack_defence = 0,
        .pack_kinship = 0.5f,
        .body_energy = 10.0f,
        .carrion_decay = 0.01f,
        .carrion_to_fertility = 0.002f,
        .fertility_max = 1.0f,
        .scent_emit = 0.05f,
        .scent_spread = 0.5f,
        .scent_decay = 0.02f,
    };
    return s;
}
