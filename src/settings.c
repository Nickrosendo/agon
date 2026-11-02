#include "settings.h"

#include <errno.h>
#include <limits.h> /* INT_MAX */
#include <stdlib.h> /* strtof */
#include <string.h> /* strcmp, strlen, strchr, strerror, memchr */

#include "options.h" /* parse_long */

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
        .veg_mutation = 0.02f,
        .veg_vigour_food = 0.6f,
        .veg_vigour_ageing = 0.6f,
        .veg_sprout_rate = 0.4f,
        .veg_grow_chance = 0.05f,
        .veg_seed_chance = 0.0002f,
        .veg_start_chance = 0.03f,
        .veg_reach = 2,
        .veg_capacity = 50.0f,
        .veg_mature_age = 60,
        .veg_wither_chance = 0.02f,
        .veg_wither_ticks = 20,
        /* Off: in a quick check (five seeds, 20,000 ticks, 200 by 100) a
         * cap of 1,500 or even 2,500 cells left only the floor's random
         * newcomers alive. Old plants nobody eats hold the cap's places,
         * and the grazed cells beside the creatures cannot sprout again. */
        .veg_max_cells = 0,
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
        /* Hunters survived on all five seeds of a quick check with it. */
        .max_plant_eaters = 500,
        .cap_by_level = 1,
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
        /* 64 samples * 25 ticks = 1,600 ticks on the graphs: longer than
         * a creature's whole life (max_age 1,500), so a rise and fall of
         * hunters fits on screen. */
        .stats_every = 25,
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
    INT_ROW(veg_birth_lo, 1, 4, "starting plants: fewest planted neighbours "
                                "to sprout"),
    INT_ROW(veg_birth_hi, 1, 7, "starting plants: most planted neighbours "
                                "to sprout (at most birth_lo + 3)"),
    INT_ROW(veg_crowd_max, 0, 7, "starting plants: most planted neighbours "
                                 "to keep growing"),
    FLOAT_ROW(veg_mutation, 0, 1, "chance a sprout's rule has a bit flipped"),
    FLOAT_ROW(veg_vigour_food, 0, 1, "how much less food vigorous plants "
                                     "give"),
    FLOAT_ROW(veg_vigour_ageing, 0, 1, "how much sooner vigorous plants "
                                       "mature"),
    FLOAT_ROW(veg_sprout_rate, 0, 1, "scales the chance to sprout beside "
                                     "plants"),
    FLOAT_ROW(veg_grow_chance, 0, 1, "chance per tick that a plant thickens"),
    FLOAT_ROW(veg_seed_chance, 0, 1, "chance of a seed from nowhere, by "
                                     "fertility"),
    FLOAT_ROW(veg_start_chance, 0, 1, "chance a soil cell starts planted"),
    INT_ROW(veg_reach, 0, 8, "radius of the local carrying capacity"),
    FLOAT_ROW(veg_capacity, 0, BIG, "most plant mass around a cell of "
                                    "fertility 1"),
    INT_ROW(veg_mature_age, 0, 65535, "ticks before a plant can wither"),
    FLOAT_ROW(veg_wither_chance, 0, 1, "chance per tick of starting to "
                                       "wither, once mature"),
    INT_ROW(veg_wither_ticks, 1, 255, "how long withering lasts"),
    INT_ROW(veg_max_cells, 0, INT_MAX, "cap on planted cells (0: no cap)"),
    INT_ROW(pool_capacity, 1, POOL_MAX, "most creatures alive at once"),
    INT_ROW(start_creatures, 0, POOL_MAX, "creatures placed in a new world"),
    INT_ROW(min_creatures, 0, POOL_MAX, "the floor: random creatures are "
                                        "added below this (0: never)"),
    {"brain", SETTING_TEXT, offsetof(Settings, brain_name), 0, 0,
     "which brain every creature uses: net or random"},
    FLOAT_ROW(energy_start, 0, BIG, "energy of creatures placed, not born"),
    FLOAT_ROW(upkeep_base, 0, BIG, "energy every tick costs"),
    FLOAT_ROW(move_cost, 0, BIG, "extra energy a move costs"),
    FLOAT_ROW(bite_veg_energy, 0, BIG, "energy in a bite of plant"),
    FLOAT_ROW(split_threshold, 0, BIG, "energy needed to split, by size"),
    FLOAT_ROW(split_share, 0, 1, "share of its energy a parent gives"),
    FLOAT_ROW(sense_age_half, 1, BIG, "the age sensed as 0.5"),
    INT_ROW(max_age, 0, INT_MAX, "death by old age, in ticks (0: never)"),
    INT_ROW(max_plant_eaters, 0, POOL_MAX, "plant-eaters cannot split above "
                                           "this (0: no cap)"),
    INT_ROW(cap_by_level, 0, 1, "count plant-eaters by level (1) or by "
                                "genes (0)"),
    FLOAT_ROW(mutation_rate, 0, 1, "chance each gene changes in a child"),
    FLOAT_ROW(mutation_spread, 0, 1, "size of a change, by the gene's range"),
    FLOAT_ROW(weight_limit, 0, 1000, "largest network weight"),
    FLOAT_ROW(weight_start_spread, 0, 1000, "spread of random weights"),
    FLOAT_ROW(size_min, 0.01, 1000, "smallest body"),
    FLOAT_ROW(size_max, 0.01, 1000, "largest body"),
    FLOAT_ROW(speed_min, 0, 1, "slowest speed, in move points per tick"),
    INT_ROW(sight_max, 1, 64, "farthest sight, in cells"),
    FLOAT_ROW(size_upkeep, 0, BIG, "upkeep per unit of size"),
    FLOAT_ROW(speed_upkeep, 0, BIG, "upkeep per unit of speed"),
    FLOAT_ROW(sight_upkeep, 0, BIG, "upkeep per cell of sight"),
    FLOAT_ROW(bite_meat_energy, 0, BIG, "most energy in a bite of carrion"),
    FLOAT_ROW(digest_floor, 0, 1, "digestion with a gene of 0"),
    FLOAT_ROW(digest_upkeep, 0, BIG, "upkeep of digesting"),
    FLOAT_ROW(attack_steepness, 0, 16, "how much size decides a fight"),
    FLOAT_ROW(attack_cost, 0, BIG, "energy an attack costs"),
    FLOAT_ROW(bite_damage, 0, BIG, "energy a hit takes, by attacker's size"),
    INT_ROW(pack_defence, 0, 1, "family strikes back at hunters (1) or not "
                                "(0)"),
    FLOAT_ROW(pack_kinship, 0, 1, "kinship that counts as family"),
    FLOAT_ROW(body_energy, 0, BIG, "carrion a body leaves, by size"),
    FLOAT_ROW(carrion_decay, 0, 1, "share of carrion lost per tick"),
    FLOAT_ROW(carrion_to_fertility, 0, 1, "share of lost carrion that feeds "
                                          "the soil"),
    FLOAT_ROW(fertility_max, 0, 1, "most fertile the soil can get"),
    FLOAT_ROW(scent_emit, 0, BIG, "scent per unit of carrion per tick"),
    FLOAT_ROW(scent_spread, 0, 1, "share of scent that spreads per tick"),
    FLOAT_ROW(scent_decay, 0, 1, "share of scent lost per tick"),
    INT_ROW(stats_every, 0, 1000000, "ticks between statistics samples "
                                     "(0: never)"),
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
