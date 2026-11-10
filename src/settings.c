#include "settings.h"

#include <errno.h>
#include <limits.h> /* INT_MAX */
#include <stdlib.h> /* strtof */
#include <string.h> /* strcmp, strlen, strchr, strerror, memchr */

#include "options.h" /* parse_long */
#include "years.h"   /* YEARS_MIN_LENGTH */

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
        .shallow_band = 0.05f,
        .temperature_scale = 80.0f,
        /* Fine enough for many blobs of rain at once. At 40, a 160 by 80 world
         * held about 8, and between 11 % and 82 % of the land had rain at the same
         * moment: a world-wide drought every so often. */
        .rain_scale = 20.0f,
        .rain_threshold = 0.6f,
        .rain_period = 1000,
        .rain_cycle = 2000,
        .rain_swing = 0.2f,
        .climate_every = 25,
        .rain_gain = 0.008f,
        .water_loss = 0.001f,
        /* A guess, from the seasons design: enough that summer shows, small
         * beside rain_swing's 0.2 so the wet and dry times still lead. */
        .summer_rain_rise = 0.1f,
        .region_cold_below = 0.33f,
        .region_hot_above = 0.6f,
        .region_dry_below = 0.25f,
        .region_wet_above = 0.7f,
        /* Plants live long and thicken slowly, but grazed ground regrows fast
         * enough to feed the creatures that sit beside it: at half these two
         * rates only 8 of 15 worlds lived to tick 20,000, and at these, 14. */
        .veg_sprout_rate = 0.05f,
        .veg_grow_chance = 0.1f,
        .veg_start_chance = 0.15f,
        .veg_reach = 2,
        .veg_capacity = 15.0f,
        .veg_mature_age = 600,
        .veg_wither_chance = 0.005f,
        .veg_wither_ticks = 20,
        /* Off: in a quick check (five seeds, 20,000 ticks, 200 by 100) a
         * cap of 1,500 or even 2,500 cells left only the floor's random
         * newcomers alive. Old plants nobody eats hold the cap's places,
         * and the grazed cells beside the creatures cannot sprout again. */
        .veg_max_cells = 0,
        /* Income high against the costs below: at plant_gain 0.05 a plant could
         * seed about once in 70 ticks, founders ate the ground bare faster than
         * that, and every world died within 5,000 ticks with fruit left hanging. */
        .plant_gain = 0.3f,
        .plant_upkeep = 0.02f,
        /* Both from the seasons design: 1.5 a starting guess, and 1 its "upkeep
         * still runs" while resting. The second is a lever if trees die in
         * every winter. */
        .spring_plenty = 1.5f,
        .plant_rest_upkeep = 1.0f,
        /* From the seasons design: 0.002 a tick over the 300 spring and summer
         * ticks of a 600-tick year is 0.6 a year, so 1 is about two years. */
        .tree_growth = 0.002f,
        .tree_size_max = 3.0f,
        .tree_shade_size = 1.0f,
        .tree_mature_age = 3000,
        .summer_heat_upkeep = 0.05f,
        .plant_energy_max = 10.0f,
        .plant_start_energy = 5.0f,
        .plant_thrift = 0.6f,
        .plant_water_min = 0.1f,
        .plant_temp_width = 0.35f,
        .plant_seed_fit = 0.3f,
        .plant_seed_energy = 3.0f,
        .plant_seed_cost = 1.0f,
        .plant_seed_start = 2.0f,
        .plant_grow_cost = 0.1f,
        .plant_mutation = 0.1f,
        .plant_mutation_spread = 0.08f,
        /* These two keep leaves and fruit both alive. Fruit is never harmed by
         * eating, so with neither, fruit trees left about 2 leaf plants in a
         * world. Thirst leaves dry ground to the leaves, and needing animals to
         * spread ties the trees to the creatures.
         * Six values here (thirst, fruit cost, leaf root, founders, a bite of
         * leaves, crowding) are one balance, Nicolas's choice on 2026-11-05.
         * Seeds 1 to 15 at 60,000 ticks: 15 alive, fruit 16 to 35 % of plants on
         * every world, still and starving 9.5 % on average (7 to 12 %), but
         * creatures of 300+ ticks only about 6 cells from birth (9.5 before). */
        .plant_fruit_thirst = 0.1f,
        .plant_fruit_near = 0.5f,
        .plant_fruit_energy = 4.0f,
        .plant_fruit_rate = 0.05f,
        .plant_fruit_cost = 1.0f,
        .plant_fruit_max = 3.0f,
        /* Doubled for the seasons (task 8): leaf plants stand bare in autumn and
         * winter, so a year's food has to be eaten in half of it. Seeds 1 to 15,
         * 60,000 ticks: 3 worlds alive with 40, 7 with 80 (and the leaf bite at
         * 36, founders at 0.05). */
        .bite_fruit_energy = 80.0f,
        .plant_gut_min = 50,
        .plant_gut_max = 400,
        .plant_dung_start = 5.0f,
        .plant_leaf_harm = 0.2f,
        /* Without it, bites ate leaf plants away: about 2 cells of 2,000 left. */
        .plant_leaf_root = 1,
        /* Sized from measurement: with plenty of room, seeds 1 to 5 peak at 5,748,
         * 1,393, 4,772, 1,000 and 3,718 over 20,000 ticks. Reaching the limit changes
         * the world (seed 1 at 4,000), so it stays well clear of the peak. */
        .pool_capacity = 8000,
        /* Kept at 0.15 through the seasons tuning (task 8), though the design asked
         * for fewer: with the food above, 0.05 left 6 of 15 worlds alive at 60,000
         * ticks, 0.10 11 and 0.15 14. The founders die off in the first winters,
         * so even at 0.15 the first 10 years were half as crowded as before on 12
         * of 15 worlds (on 14 of 15 at 0.10). */
        .start_share = 0.15f,
        .min_creatures = 0,
        /* A char array inside a struct can be initialised from a string
         * literal: the characters are copied in and the rest is zero. */
        .brain_name = "net",
        .energy_start = 60.0f,
        /* Slowed at Nicolas's request: 0.1 spent the starting energy in 1.3 years. */
        .upkeep_base = 0.06f,
        .move_cost = 0.3f,
        /* Doubled for the seasons (task 8). At 18 every world died out within 60
         * years; at 27 11 of 15 lived, at 36 14 of 15 (with fruit at 80, meat at
         * 16 and the founders at 0.15). */
        .bite_veg_energy = 36.0f,
        .mate_threshold = 30.0f,
        .mate_share = 0.5f,
        .mate_barrier = 0.55f,
        .mate_window = 5,
        /* 2 since 2026-11-08, Nicolas's pick: over 100 years on 15 seeds,
         * all 15 worlds lived (beside only: 11). At 1 the founders' die-off
         * was followed by years of decline, births half the deaths, until
         * four worlds died; the pairs were there but a step too far apart. */
        .mate_reach = 2,
        .mate_age = 50,
        .sense_age_half = 200.0f,
        /* A season: the probe of 2026-11-08 found most starving adults had gone
         * a season or more without a meal, biting at nothing on one spot. */
        .fed_fade_ticks = 150,
        .ageing_upkeep = 0.3f,
        /* 1500 until the seasons (task 8): a fed size 8 body with middling genes
         * then starved of its own age before 4 years (2,000 still did, 2,500 was
         * the first to pass; test_fed_giant_lives_four_years). */
        .ageing_span = 3000.0f,
        .ageing_min_rate = 0.25f,
        .slow_ageing_upkeep = 0.05f,
        .max_age = 0,
        .max_plant_eaters = 0,
        .cap_by_level = 1,
        .birth_room = 0,
        .birth_crowd_price = 0.0f,
        .birth_capacity = 0,
        /* On since 2026-11-07, Nicolas's pick of the four birth rules: over
         * 100 years on 15 seeds, 11 worlds lived (the fixed capacity: 9), the
         * survivors levelled at 1,350-2,400 instead of booming to 5,000, and
         * adults lived 2,538 ticks on average instead of 582. 0.75 lost 7
         * worlds. The other three rules stay, off, for comparisons. 0.4 since
         * 2026-11-08, with a partner two steps away: still 15 of 15 worlds,
         * adults 2,475 ticks instead of 2,329, a fifth fewer creatures. */
        .birth_per_plant = 0.4f,
        .mutation_rate = 0.05f,
        .mutation_spread = 0.05f,
        .weight_limit = 4.0f,
        .weight_start_spread = 1.0f,
        .size_min = 0.5f,
        .birth_size_max = 1.5f,
        /* The energy kept back is what carries a creature between meals. Measured
         * on seeds 1 to 15, 20,000 ticks: alive on 0 of 15 at 1.5, 3 at 6, 12 at 10,
         * 11 at 15 and 12 at 25. Burning the surplus with almost no body to show for
         * it ended worlds as fast, so the reserve is what matters, not the body. This
         * is the lowest value on the plateau. */
        .growth_surplus = 10.0f,
        .growth_cost = 50.0f,
        .speed_min = 0.25f,
        .sight_max = 6,
        .size_upkeep = 0.1f,
        .speed_upkeep = 0.1f,
        .sight_upkeep = 0.01f,
        /* Doubled for the seasons (task 8), the design's "larger rise" for meat.
         * Measured alone it changed little: 6 worlds of 15 alive against 7. */
        .bite_meat_energy = 16.0f,
        .digest_floor = 0.25f,
        .digest_upkeep = 0.025f,
        .digest_tradeoff = 0.3f,
        .camouflage = 0.6f,
        .camouflage_width = 0.15f,
        .cold_upkeep = 0.2f,
        .heat_upkeep = 0.05f,
        .rough_step_cost = 0.3f,
        .legs_step_cost = 0.1f,
        .swim_step_cost = 0.1f,
        .attack_steepness = 3.0f,
        .attack_cost = 0.5f,
        .gang_bonus = 0.5f,
        .gang_window = 10,
        .armour_block = 0.7f,
        .weapon_damage = 1.0f,
        .armour_upkeep = 0.05f,
        .weapon_upkeep = 0.05f,
        .health_per_size = 30.0f,
        .heal_rate = 0.1f,
        .heal_cost = 1.0f,
        .starve_damage = 0.5f,
        /* Off: measured on seeds 1 to 5 it made worlds die without reducing how much
         * creatures stand still (2 to 3 of 5 alive at 0.05 and above). */
        .bite_empty_cost = 0.0f,
        .bite_damage = 6.0f,
        .kin_sense = 0.5f,
        /* Nicolas, 2026-11-03: a creature leaves its family without turning on it.
         * Measured on seeds 1 to 15: hits within a family name fell from 57 % to
         * 37 %, none are now between close relatives, and families rose from 3.8 to
         * 9.7 on average. */
        .family_peace = 0.3f,
        /* Nicolas, 2026-11-04. Seeds 1 to 15: hits between similar colours fell from
         * about half to none, hits within a family name from 44 % to 9 %, and deaths
         * from wounds from 26 % to 13 %. */
        .colour_peace = 0.1f,
        .family_split = 0.25f,
        .rest_upkeep = 0.5f,
        /* The seasons design, section 6: a starting guess, against resting's 0.5. */
        .sleep_upkeep = 0.1f,
        /* Nicolas's design, 2026-11-07: hibernation only in winter, prepared for
         * by eating, and slept through to spring; a nap costs what rest does and
         * lasts at least 10 ticks. 30 ticks for every sleep cost 5 of 15 worlds
         * over 100 years (small ones). 3x what mating needs: an adult's median
         * store at the start of winter was 4 to 6x. */
        .hibernate_energy = 3.0f,
        .nap_min_ticks = 10,
        /* These two together, Nicolas's call to try. Seeds 1 to 15, 20,000 ticks, the
         * best of six tried: 15 alive and the smallest world at 98, deaths from wounds
         * 26.3 % against 2.4 %, still and starving 7 % against 20 %. Alone, territory
         * lost a world; stronger crowding lost one or two and left worlds of under 70.
         * Later, with leaf roots kept and three times the founders, crowding at
         * 0.08 is what keeps still-and-starving deaths near 10 % (2026-11-05).
         * The drive halved to 0.15 on 2026-11-09, Nicolas's pick: once the plant
         * ray saw only food, creatures left spent spots and met strangers far
         * more often, and 97 % of kills were this drive. Seasons verdict, 100
         * years: adults live 2,702 ticks instead of 2,251, wounds 29 % of adult
         * deaths (0 gave 3,575 and only 12 %). */
        .territory_drive = 0.15f,
        .territory_upkeep = 0.05f,
        .crowd_upkeep = 0.08f,
        .crowd_radius = 2,
        .crowd_free = 1,
        .body_energy = 10.0f,
        .carrion_decay = 0.01f,
        /* Off: at 0.002, rotting carrion slowly enriched the soil and plants
         * crept up to 17 to 20 % of it by tick 150,000. Off, they hold at 10
         * to 13 %. Set it above 0 to have carrion feed the soil again. */
        .carrion_to_fertility = 0.0f,
        .fertility_max = 1.0f,
        .scent_emit = 0.05f,
        .scent_spread = 0.5f,
        .scent_decay = 0.02f,
        /* 64 samples * 25 ticks = 1,600 ticks on the graphs: longer than
         * many creatures' whole lives, so a rise and fall of hunters fits
         * on screen. */
        .stats_every = 25,
        .ticks_per_year = 600,
        /* The tactics design: 10 days a year, 30 seconds a day at 2 ticks a
         * second. */
        .ticks_per_day = 60,
        .blind_senses = 0,
        .call_cost = 0.05f,
        .call_reach = 6,
        .reward_meal = 0.05f,
        .reward_pain = 1.0f,
        .learn_rate_max = 0.05f,
        .learn_trace_fade = 0.9f,
        .learn_limit = 2.0f,
        .learn_upkeep = 0.05f,
        .night_eye_power = 0.8f,
        .night_glare = 0.4f,
        .night_eye_upkeep = 0.05f,
        .chronicle = 1,
        .tactics = 1,
        .tactic_lift = 2.0f,
        .tactic_min = 10,
        .tactic_useful = 1.2f,
        .giant_size = 6.0f,
    };
    return s;
}

/* The largest value a float setting may take where nothing else limits
 * it: far above any sensible setting, far below where sums overflow. */
#define BIG 1000000.0
/* The most creatures a world may hold: about 800 MB of creatures. A save
 * file's settings pass the same check, so a damaged file cannot ask for
 * more. */
#define POOL_MAX 1000000

/*
 * Rows of the table. offsetof(Settings, field), from <stddef.h>, is the
 * field's distance in bytes from the start of the struct, worked out by
 * the compiler, padding and all. #field turns the field's name into a
 * string, as CHECK does in tests/test.h. So one macro argument gives both
 * the name in the file and the place in the struct, and they cannot
 * disagree.
 *
 * The macro cannot tell whether the field really is an int or a float.
 * tests/test_settings.c catches a wrong row: the bits of an int read as a
 * float (or the other way round) fall outside the range.
 */
#define INT_ROW(field, lo, hi, text) \
    {#field, SETTING_INT, offsetof(Settings, field), lo, hi, text}
#define FLOAT_ROW(field, lo, hi, text) \
    {#field, SETTING_FLOAT, offsetof(Settings, field), lo, hi, text}

/* In the struct's order. The save file writes settings in this order
 * too: changing the table changes the file format (see SAVE_VERSION). */
static const SettingInfo TABLE[] = {
    FLOAT_ROW(water_level, 0, 1, "heights below this are water"),
    FLOAT_ROW(rock_level, 0, 1, "heights from this up are rock"),
    FLOAT_ROW(noise_scale, 1, BIG, "cells between terrain lattice points"),
    INT_ROW(noise_octaves, 1, 16, "layers of terrain noise"),
    FLOAT_ROW(fertility_scale, 1, BIG, "the same, for the fertility field"),
    FLOAT_ROW(shallow_band, 0, 1, "heights this far below water_level are shallow"),
    FLOAT_ROW(temperature_scale, 1, BIG, "the same, for the temperature field"),
    FLOAT_ROW(rain_scale, 1, BIG, "the same, for the rain"),
    FLOAT_ROW(rain_threshold, 0, 1, "rain noise needed to rain (0 everywhere, 1 nowhere)"),
    INT_ROW(rain_period, 1, INT_MAX, "ticks for one rain pattern to fade into the next"),
    INT_ROW(rain_cycle, 1, INT_MAX, "ticks from one wet time to the next"),
    FLOAT_ROW(rain_swing, 0, 1, "how far the threshold moves either way between wet and dry"),
    INT_ROW(climate_every, 1, 65535, "ticks between updates of rain and water"),
    FLOAT_ROW(rain_gain, 0, 1, "water soil takes per tick in full rain"),
    FLOAT_ROW(water_loss, 0, 1, "share of its water soil loses per tick"),
    FLOAT_ROW(summer_rain_rise, 0, 1, "added to the rain threshold in summer, so less rain"),
    FLOAT_ROW(region_cold_below, 0, 1, "temperature below this is a cold region"),
    FLOAT_ROW(region_hot_above, 0, 1, "temperature above this can be hot and dry"),
    FLOAT_ROW(region_dry_below, 0, 1, "water below this, when hot, is hot and dry"),
    FLOAT_ROW(region_wet_above, 0, 1, "water above this is a wet region"),
    FLOAT_ROW(veg_sprout_rate, 0, 1, "scales the chance to sprout beside "
                                     "plants"),
    FLOAT_ROW(veg_grow_chance, 0, 1, "chance per tick that a plant thickens"),
    FLOAT_ROW(veg_start_chance, 0, 1, "chance a soil cell starts planted"),
    INT_ROW(veg_reach, 0, 8, "radius of the local carrying capacity"),
    FLOAT_ROW(veg_capacity, 0, BIG, "most plant mass around a cell of "
                                    "fertility 1"),
    INT_ROW(veg_mature_age, 0, 65535, "ticks before a plant can wither"),
    FLOAT_ROW(veg_wither_chance, 0, 1, "chance per tick of starting to "
                                       "wither, once mature"),
    INT_ROW(veg_wither_ticks, 1, 255, "how long withering lasts"),
    INT_ROW(veg_max_cells, 0, INT_MAX, "cap on planted cells (0: no cap)"),
    FLOAT_ROW(plant_gain, 0, BIG, "energy a plant gains per tick where it fits best"),
    FLOAT_ROW(plant_upkeep, 0, BIG, "energy a plant pays per tick"),
    FLOAT_ROW(spring_plenty, 1, 10, "plant income and fruit in spring, times the usual"),
    FLOAT_ROW(plant_rest_upkeep, 0, 1, "share of its upkeep a plant pays while resting"),
    FLOAT_ROW(tree_growth, 0, 1, "size a leaf tree gains per spring or summer tick"),
    FLOAT_ROW(tree_size_max, 0, 100, "largest size a leaf tree grows to"),
    FLOAT_ROW(tree_shade_size, 0, 100, "size from which a leaf tree gives shade"),
    INT_ROW(tree_mature_age, 0, 65535, "ticks before a leaf tree can wither"),
    FLOAT_ROW(summer_heat_upkeep, 0, BIG, "energy a creature out of shade pays per summer tick"),
    FLOAT_ROW(plant_energy_max, 0, BIG, "most energy a plant can store"),
    FLOAT_ROW(plant_start_energy, 0, BIG, "energy of a starting plant"),
    FLOAT_ROW(plant_thrift, 0, 1, "growth of a plant needing no water, against a thirsty one"),
    FLOAT_ROW(plant_water_min, 0.001, 1, "water even the hardiest plant needs"),
    FLOAT_ROW(plant_temp_width, 0.001, 1, "how far from its best temperature a plant still lives"),
    FLOAT_ROW(plant_seed_fit, 0, 1, "how well a place must suit a plant before it seeds, grows or fruits"),
    FLOAT_ROW(plant_seed_energy, 0, BIG, "energy a plant must hold to seed"),
    FLOAT_ROW(plant_seed_cost, 0, BIG, "energy a seed costs its parent"),
    FLOAT_ROW(plant_seed_start, 0, BIG, "energy of a sprout beside its parent"),
    FLOAT_ROW(plant_grow_cost, 0, BIG, "energy a plant pays to thicken by one step"),
    FLOAT_ROW(plant_mutation, 0, 1, "chance each gene of a seed changes"),
    FLOAT_ROW(plant_mutation_spread, 0, 1, "size of that change"),
    FLOAT_ROW(plant_fruit_thirst, 0, BIG, "water a fruit tree needs beyond what its gene says"),
    FLOAT_ROW(plant_fruit_near, 0, 1, "how often a fruit tree seeds beside itself, against a leaf plant"),
    FLOAT_ROW(plant_fruit_energy, 0, BIG, "energy a plant must hold to make fruit"),
    FLOAT_ROW(plant_fruit_rate, 0, BIG, "fruit per tick of a plant offering only fruit"),
    FLOAT_ROW(plant_fruit_cost, 0, BIG, "energy one fruit costs its plant"),
    FLOAT_ROW(plant_fruit_max, 0, BIG, "most fruit on one plant"),
    FLOAT_ROW(bite_fruit_energy, 0, BIG, "energy in one fruit"),
    INT_ROW(plant_gut_min, 1, 65535, "ticks a seed stays in an animal, at least"),
    INT_ROW(plant_gut_max, 1, 65535, "ticks a seed stays in an animal, at most"),
    FLOAT_ROW(plant_dung_start, 0, BIG, "energy of a sprout from dropped dung"),
    FLOAT_ROW(plant_leaf_harm, 0, BIG, "plant energy lost per density step of leaves eaten"),
    INT_ROW(plant_leaf_root, 0, 3, "density steps of a leaf plant no bite can take"),
    INT_ROW(pool_capacity, 1, POOL_MAX, "most creatures alive at once"),
    FLOAT_ROW(start_share, 0, 1, "founders of a new world, as a share of its "
                                 "soil"),
    INT_ROW(min_creatures, 0, POOL_MAX, "the floor: random creatures are "
                                        "added below this (0: never)"),
    {"brain", SETTING_TEXT, offsetof(Settings, brain_name), 0, 0,
     "which brain every creature uses: net or random"},
    FLOAT_ROW(energy_start, 0, BIG, "energy of creatures placed, not born"),
    FLOAT_ROW(upkeep_base, 0, BIG, "energy every tick costs"),
    FLOAT_ROW(move_cost, 0, BIG, "extra energy a move costs"),
    FLOAT_ROW(bite_veg_energy, 0, BIG, "energy in a bite of plant"),
    FLOAT_ROW(mate_threshold, 0, BIG, "energy needed to mate, by size"),
    FLOAT_ROW(mate_share, 0, 1, "share of its energy each parent gives"),
    FLOAT_ROW(mate_barrier, 0, 1, "bodies less alike than this have no child (0 is off)"),
    INT_ROW(mate_window, 1, 65535, "ticks a wish to mate lasts"),
    INT_ROW(mate_reach, 1, 4, "steps a partner may stand away (1: beside)"),
    INT_ROW(mate_age, 0, INT_MAX, "ticks before a creature can mate"),
    FLOAT_ROW(sense_age_half, 1, BIG, "the age sensed as 0.5"),
    INT_ROW(fed_fade_ticks, 1, 65535, "ticks without a meal for the fed sense to fall to 0"),
    FLOAT_ROW(ageing_upkeep, 0, BIG, "what age costs at the span below"),
    FLOAT_ROW(ageing_span, 1, BIG, "ticks for quick genes to reach that cost"),
    FLOAT_ROW(ageing_min_rate, 0, 1, "how fast the slowest genes age, as a "
                                     "share"),
    FLOAT_ROW(slow_ageing_upkeep, 0, BIG, "upkeep paid for the slowest ageing"),
    INT_ROW(max_age, 0, INT_MAX, "a fixed death age, in ticks (0: none)"),
    INT_ROW(max_plant_eaters, 0, POOL_MAX, "plant-eaters cannot mate above "
                                           "this (0: no cap)"),
    INT_ROW(cap_by_level, 0, 1, "count plant-eaters by level (1) or by "
                                "genes (0)"),
    INT_ROW(birth_room, 0, 288, "no child while more neighbours than this "
                                "(0: off)"),
    FLOAT_ROW(birth_crowd_price, 0, BIG, "a child's extra price per crowding "
                                         "neighbour, as a share (0: off)"),
    INT_ROW(birth_capacity, 0, POOL_MAX, "births slow as the alive near this "
                                         "(0: off)"),
    FLOAT_ROW(birth_per_plant, 0, BIG, "births slow as the alive near this many "
                                       "per plant (0: off)"),
    FLOAT_ROW(mutation_rate, 0, 1, "chance each gene changes in a child"),
    FLOAT_ROW(mutation_spread, 0, 1, "size of a change, by the gene's range"),
    FLOAT_ROW(weight_limit, 0, 1000, "largest network weight"),
    FLOAT_ROW(weight_start_spread, 0, 1000, "spread of random weights"),
    FLOAT_ROW(size_min, 0.01, 1000, "smallest body"),
    FLOAT_ROW(birth_size_max, 0.01, 1000, "largest body at birth"),
    FLOAT_ROW(growth_surplus, 0, BIG, "surplus begins at this share of mating's need"),
    FLOAT_ROW(growth_cost, 0.001, BIG, "energy per unit of size grown"),
    FLOAT_ROW(speed_min, 0, 1, "slowest speed, in move points per tick"),
    INT_ROW(sight_max, 1, 64, "farthest sight, in cells"),
    FLOAT_ROW(size_upkeep, 0, BIG, "upkeep per unit of size"),
    FLOAT_ROW(speed_upkeep, 0, BIG, "upkeep per unit of speed"),
    FLOAT_ROW(sight_upkeep, 0, BIG, "upkeep per cell of sight"),
    FLOAT_ROW(bite_meat_energy, 0, BIG, "most energy in a bite of carrion"),
    FLOAT_ROW(digest_floor, 0, 1, "digestion with a gene of 0"),
    FLOAT_ROW(digest_upkeep, 0, BIG, "upkeep of digesting"),
    FLOAT_ROW(digest_tradeoff, 0, 1, "how much the strongest other digestion gene cuts a yield"),
    FLOAT_ROW(camouflage, 0, 1, "how much of the sight distance a perfect colour match hides"),
    FLOAT_ROW(camouflage_width, 0.01, 0.5, "colour distance at which camouflage stops helping"),
    FLOAT_ROW(cold_upkeep, 0, BIG, "energy per tick in the cold, divided by size"),
    FLOAT_ROW(heat_upkeep, 0, BIG, "energy per tick in hot dry ground, times size"),
    FLOAT_ROW(rough_step_cost, 0, BIG, "extra energy for a step on marsh, sand or shallows"),
    FLOAT_ROW(legs_step_cost, 0, BIG, "extra energy for long legs on other ground"),
    FLOAT_ROW(swim_step_cost, 0, BIG, "extra energy for a swimming body on land"),
    FLOAT_ROW(attack_steepness, 0, 16, "how much size decides a fight"),
    FLOAT_ROW(attack_cost, 0, BIG, "energy an attack costs"),
    FLOAT_ROW(gang_bonus, 0, BIG, "better odds per other recent attacker"),
    INT_ROW(gang_window, 0, 65535, "ticks a hit counts towards those odds"),
    FLOAT_ROW(armour_block, 0, BIG, "share of a hit full armour takes off"),
    FLOAT_ROW(weapon_damage, 0, BIG, "share a full weapon adds to a hit"),
    FLOAT_ROW(armour_upkeep, 0, BIG, "upkeep of full armour"),
    FLOAT_ROW(weapon_upkeep, 0, BIG, "upkeep of a full weapon"),
    FLOAT_ROW(health_per_size, 0.001, BIG, "health of an unhurt body, by size"),
    FLOAT_ROW(heal_rate, 0, BIG, "health healed per tick when fed"),
    FLOAT_ROW(heal_cost, 0, BIG, "energy per point of health healed"),
    FLOAT_ROW(starve_damage, 0, BIG, "health lost per tick at zero energy"),
    FLOAT_ROW(bite_empty_cost, 0, BIG, "energy a bite that finds nothing costs"),
    FLOAT_ROW(bite_damage, 0, BIG, "health a hit takes, by attacker's size"),
    FLOAT_ROW(kin_sense, 0, 1, "kinship from which a neighbour counts as family"),
    FLOAT_ROW(family_peace, 0, 2, "kinship from which an attack does not strike"),
    FLOAT_ROW(colour_peace, 0, 0.5, "colour distance within which an attack does not strike"),
    FLOAT_ROW(family_split, 0, 0.5, "colour drift at which a newborn starts a family"),
    FLOAT_ROW(rest_upkeep, 0, 1, "share of upkeep paid while resting"),
    FLOAT_ROW(sleep_upkeep, 0, 1, "share of upkeep and summer heat paid in hibernation"),
    FLOAT_ROW(hibernate_energy, 0, BIG, "stores, in matings' worth, needed to hibernate in winter"),
    INT_ROW(nap_min_ticks, 0, 65535, "fewest ticks a nap lasts (0: no minimum)"),
    FLOAT_ROW(territory_drive, 0, 1, "chance a full territory gene attacks a stranger beside it"),
    FLOAT_ROW(territory_upkeep, 0, BIG, "upkeep of a full territory gene"),
    FLOAT_ROW(crowd_upkeep, 0, BIG, "energy per tick per neighbour past the free ones"),
    INT_ROW(crowd_radius, 1, 8, "how many cells away counts as a neighbour"),
    INT_ROW(crowd_free, 0, 288, "neighbours that cost nothing"),
    FLOAT_ROW(body_energy, 0, BIG, "carrion a body leaves, by size"),
    FLOAT_ROW(carrion_decay, 0, 1, "share of carrion lost per tick"),
    FLOAT_ROW(carrion_to_fertility, 0, 1, "share of lost carrion that feeds "
                                          "the soil (0: none)"),
    FLOAT_ROW(fertility_max, 0, 1, "most fertile the soil can get"),
    FLOAT_ROW(scent_emit, 0, BIG, "scent per unit of carrion per tick"),
    FLOAT_ROW(scent_spread, 0, 1, "share of scent that spreads per tick"),
    FLOAT_ROW(scent_decay, 0, 1, "share of scent lost per tick"),
    INT_ROW(stats_every, 0, 1000000, "ticks between statistics samples "
                                     "(0: never)"),
    INT_ROW(ticks_per_year, YEARS_MIN_LENGTH, 1000000,
            "ticks in a year of four seasons, spring first; spring is the "
            "mating season"),
    INT_ROW(ticks_per_day, YEARS_MIN_DAY, 1000000,
            "ticks in a day, noon first; sight fails in the dark"),
    INT_ROW(blind_senses, 0, 31, "senses held at 0 for every brain: 1 day, 2 a "
                                 "fight seen, 4 a stray seen, 8 kin hit, 16 hearing"),
    FLOAT_ROW(call_cost, 0, BIG, "energy a tick of calling costs"),
    INT_ROW(call_reach, 1, 32, "how many cells away a call is heard"),
    FLOAT_ROW(reward_meal, 0, BIG, "how much a unit of energy eaten teaches"),
    FLOAT_ROW(reward_pain, 0, BIG, "how much losing all its health to hits teaches"),
    FLOAT_ROW(learn_rate_max, 0, BIG, "learning rate at the full learning gene"),
    FLOAT_ROW(learn_trace_fade, 0, 1, "share of the trace of a choice kept each tick"),
    FLOAT_ROW(learn_limit, 0, 1000, "largest learned weight"),
    FLOAT_ROW(learn_upkeep, 0, BIG, "upkeep of the full learning gene"),
    FLOAT_ROW(night_eye_power, 0, 1, "share of its sight a creature with full "
                                     "night eyes keeps at midnight"),
    FLOAT_ROW(night_glare, 0, 1, "share of its sight full night eyes lose at noon"),
    FLOAT_ROW(night_eye_upkeep, 0, BIG, "upkeep of full night eyes"),
    INT_ROW(chronicle, 0, 1, "1: record life stories, 0: record none "
                             "(changes no world)"),
    INT_ROW(tactics, 0, 1, "1: watch for tactics, 0: do not (changes no world)"),
    FLOAT_ROW(tactic_lift, 1, 100, "how many times chance a family's habit "
                                   "must be to count as a tactic"),
    INT_ROW(tactic_min, 1, 100000, "events of the last year a tactic needs"),
    FLOAT_ROW(tactic_useful, 1, 100, "how many times better than the others "
                                     "of its diet a family with a tactic does"),
    FLOAT_ROW(giant_size, 0, BIG, "body size from which a life story counts a "
                                  "giant"),
};

int settings_count(void)
{
    return (int)(sizeof(TABLE) / sizeof(TABLE[0]));
}

const SettingInfo *settings_info(int i)
{
    return &TABLE[i];
}

/*
 * From an offset back to a field. Pointer arithmetic counts in elements of
 * the pointed-to type, so to move by bytes the Settings pointer first
 * becomes a char pointer, whose element is one byte. Then it becomes a
 * pointer to the field's real type. This is legal because the memory at
 * that offset really does hold an int (or float): the cast only tells the
 * compiler what is already true. Nothing checks it.
 */
int *settings_int(Settings *s, const SettingInfo *info)
{
    return (int *)(void *)((char *)s + info->offset);
}

float *settings_float(Settings *s, const SettingInfo *info)
{
    return (float *)(void *)((char *)s + info->offset);
}

char *settings_text(Settings *s, const SettingInfo *info)
{
    return (char *)s + info->offset;
}

/* Spaces, tabs and the '\r' of a file saved on Windows. */
static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Cuts spaces off both ends of a string, in place: writes a new '\0' over
 * the end, and returns a pointer to the first character that is kept. */
static char *trim(char *text)
{
    while (is_space(*text)) {
        text++;
    }
    size_t len = strlen(text);
    while (len > 0 && is_space(text[len - 1])) {
        len--;
    }
    text[len] = '\0';
    return text;
}

static const SettingInfo *find_setting(const char *name)
{
    for (int i = 0; i < settings_count(); i++) {
        if (strcmp(TABLE[i].name, name) == 0) {
            return &TABLE[i];
        }
    }
    return NULL;
}

/* Asks "inside the range", not "below or above it": every comparison with
 * NaN is false, so this form says no to NaN and the other would say it is
 * fine. */
static bool in_range(double v, const SettingInfo *info)
{
    return v >= info->min && v <= info->max;
}

static bool parse_int_value(const char *text, const SettingInfo *info, int *out)
{
    long v;
    if (!parse_long(text, (long)info->min, (long)info->max, &v)) {
        return false;
    }
    *out = (int)v; /* safe: every row's range fits an int */
    return true;
}

/* strtof alone is too forgiving, as strtol is: it stops quietly at the
 * first bad character, and accepts "nan" and "inf". */
static bool parse_float_value(const char *text, const SettingInfo *info,
                              float *out)
{
    char *end;
    errno = 0;
    float v = strtof(text, &end);
    if (end == text || *end != '\0' || errno == ERANGE ||
        !in_range((double)v, info)) {
        return false;
    }
    *out = v;
    return true;
}

static bool parse_text_value(const char *text, char *out)
{
    size_t len = strlen(text);
    /* len + 1: the name and its '\0' must both fit. */
    if (len == 0 || len + 1 > BRAIN_NAME_MAX || strchr(text, ' ') != NULL ||
        strchr(text, '\t') != NULL) {
        return false;
    }
    memcpy(out, text, len + 1);
    return true;
}

/* Writes "name: wants ..." into err. */
static void bad_value(const SettingInfo *info, const char *value, char *err,
                      size_t errlen)
{
    switch (info->type) {
    case SETTING_INT:
        snprintf(err, errlen, "%s: '%s' is not a whole number from %.0f to %.0f",
                 info->name, value, info->min, info->max);
        break;
    case SETTING_FLOAT:
        snprintf(err, errlen, "%s: '%s' is not a number from %g to %g",
                 info->name, value, info->min, info->max);
        break;
    default:
        snprintf(err, errlen, "%s: '%s' is not one word of at most %d letters",
                 info->name, value, BRAIN_NAME_MAX - 1);
    }
}

/* One line, already without its comment. The line is changed in place
 * (trim writes '\0's into it). False with a message in err. */
static bool load_line(Settings *s, char *line, char *err, size_t errlen)
{
    char *text = trim(line);
    if (text[0] == '\0') {
        return true;
    }
    if (text[0] == '[') {
        if (text[strlen(text) - 1] != ']') {
            snprintf(err, errlen, "a [section] line needs its closing ]");
            return false;
        }
        return true;
    }
    char *equals = strchr(text, '=');
    if (equals == NULL) {
        snprintf(err, errlen, "'%s' needs the form name = value", text);
        return false;
    }
    *equals = '\0'; /* one string becomes two: the name and the value */
    char *name = trim(text);
    char *value = trim(equals + 1);
    if (name[0] == '\0') {
        snprintf(err, errlen, "no name before the = sign");
        return false;
    }
    const SettingInfo *info = find_setting(name);
    if (info == NULL) {
        snprintf(err, errlen, "unknown setting '%s'", name);
        return false;
    }
    bool ok;
    switch (info->type) {
    case SETTING_INT:
        ok = parse_int_value(value, info, settings_int(s, info));
        break;
    case SETTING_FLOAT:
        ok = parse_float_value(value, info, settings_float(s, info));
        break;
    default:
        ok = parse_text_value(value, settings_text(s, info));
    }
    if (!ok) {
        bad_value(info, value, err, errlen);
    }
    return ok;
}

/* Reads every line into *s. False with a message naming the line. */
static bool load_lines(Settings *s, FILE *f, const char *path, char *err,
                       size_t errlen)
{
    char line[SETTINGS_LINE_MAX];
    char problem[SETTINGS_LINE_MAX + 64];
    int number = 0;
    /* fgets reads up to and including a newline, or until the buffer is
     * full, always ending with '\0'. NULL means end of file or an error. */
    while (fgets(line, sizeof(line), f) != NULL) {
        number++;
        size_t len = strlen(line);
        /* The buffer filled up before any newline. Without this check the
         * rest of the line would come back from the next fgets, looking
         * like a line of its own. */
        if (len == sizeof(line) - 1 && line[len - 1] != '\n') {
            snprintf(err, errlen, "%s line %d: longer than %d characters", path,
                     number, SETTINGS_LINE_MAX - 2);
            return false;
        }
        char *comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        if (!load_line(s, line, problem, sizeof(problem))) {
            snprintf(err, errlen, "%s line %d: %s", path, number, problem);
            return false;
        }
    }
    /* NULL from fgets does not say which of the two happened. */
    if (ferror(f)) {
        snprintf(err, errlen, "%s line %d: could not read: %s", path,
                 number + 1, strerror(errno));
        return false;
    }
    return true;
}

bool settings_load(Settings *s, const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        /* errno is the C library's last error, a plain global number that
         * only means something straight after a call that failed.
         * strerror turns it into words: "No such file or directory". */
        snprintf(err, errlen, "cannot open %s: %s", path, strerror(errno));
        return false;
    }
    /* Work on a copy, and copy it back only when the whole file was good:
     * a mistake on line 40 must not leave lines 1 to 39 applied. */
    Settings copy = *s;
    bool ok = load_lines(&copy, f, path, err, errlen);
    fclose(f);
    if (ok) {
        *s = copy;
    }
    return ok;
}

bool settings_check(const Settings *s, char *err, size_t errlen)
{
    /* The accessors take a non-const Settings because they serve writers
     * too. Nothing is written here, so a local copy keeps the promise of
     * const without a cast. */
    Settings copy = *s;
    for (int i = 0; i < settings_count(); i++) {
        const SettingInfo *info = &TABLE[i];
        switch (info->type) {
        case SETTING_INT: {
            int v = *settings_int(&copy, info);
            if (!in_range((double)v, info)) {
                snprintf(err, errlen, "%s: %d is outside %.0f to %.0f",
                         info->name, v, info->min, info->max);
                return false;
            }
            break;
        }
        case SETTING_FLOAT: {
            float v = *settings_float(&copy, info);
            if (!in_range((double)v, info)) {
                snprintf(err, errlen, "%s: %g is outside %g to %g", info->name,
                         (double)v, info->min, info->max);
                return false;
            }
            break;
        }
        default: {
            const char *text = settings_text(&copy, info);
            /* memchr looks for a byte within a fixed length, and so never
             * runs off the end as strlen would without a '\0'. */
            if (memchr(text, '\0', BRAIN_NAME_MAX) == NULL ||
                text[0] == '\0') {
                snprintf(err, errlen, "%s: not a name", info->name);
                return false;
            }
        }
        }
    }
    return true;
}

/* A float with as few digits as give the same float back: 0.38 rather
 * than 0.379999995. Nine significant digits are always enough. */
static void print_float(FILE *out, float v)
{
    char text[32];
    snprintf(text, sizeof(text), "%g", (double)v);
    if (strtof(text, NULL) != v) {
        snprintf(text, sizeof(text), "%.9g", (double)v);
    }
    fputs(text, out);
}

void settings_print(FILE *out, const Settings *s)
{
    Settings copy = *s;
    fputs("# Settings for agon: agon --config this-file\n"
          "# Lines are name = value. Leave out any line to keep its default.\n"
          "# Generated by: agon --settings\n",
          out);
    for (int i = 0; i < settings_count(); i++) {
        const SettingInfo *info = &TABLE[i];
        switch (info->type) {
        case SETTING_INT:
            fprintf(out, "\n# %s (%.0f to %.0f)\n%s = %d\n", info->help,
                    info->min, info->max, info->name,
                    *settings_int(&copy, info));
            break;
        case SETTING_FLOAT:
            fprintf(out, "\n# %s (%g to %g)\n%s = ", info->help, info->min,
                    info->max, info->name);
            print_float(out, *settings_float(&copy, info));
            fputc('\n', out);
            break;
        default:
            fprintf(out, "\n# %s\n%s = %s\n", info->help, info->name,
                    settings_text(&copy, info));
        }
    }
}
