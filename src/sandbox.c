#include "sandbox.h"

#include <stdio.h> /* snprintf */

#include "actions.h"
#include "chronicle.h" /* name_from_id */
#include "genome.h"
#include "weather.h"

/* The window's key codes for Enter and Escape. Enter arrives as the carriage
 * return character; Escape has a code of its own (window/platform.h), which a
 * core file cannot include, so the number is repeated here and checked by
 * tests/test_sandbox.c. */
#define KEY_ENTER '\r'
#define SANDBOX_KEY_ESCAPE 1004

/* The three skips offered, in years, on keys 1 to 3. */
static const int SKIP_YEARS[3] = {1, 5, 20};

/* The setup screen's steps. 1 and 0.02 are the defaults (settings.c), so a
 * screen opened on a plain world and closed with Enter changes nothing. */
static const float WILD_STEPS[SANDBOX_WILD_STEPS] = {0.5f, 1.0f, 2.0f, 3.0f,
                                                     5.0f, 10.0f};
static const float JUMP_STEPS[SANDBOX_JUMP_STEPS] = {0.01f, 0.02f, 0.05f,
                                                     0.1f, 0.2f};

/* The browser demo (the player agency design, section 9) keeps weather only:
 * this says so when a cut tool's key is pressed. #ifdef keeps it out of the
 * full build, where nothing calls it: a static function never called draws an
 * "unused function" warning. */
#ifdef AGON_DEMO
static int not_in_demo(char *notice, size_t size)
{
    snprintf(notice, size, "not in the browser demo: weather only (w)");
    return SANDBOX_TAKEN;
}
#endif

static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
}

/* The digit keys '1'.. as 0.., or -1 for another key. Characters are numbers
 * in C, and the digits are in order, so key - '1' is the digit less one. */
static int digit(int key, int count)
{
    int d = key - '1';
    return d >= 0 && d < count ? d : -1;
}

static void open_trait(Sandbox *b, const Creature *followed)
{
    b->menu = SANDBOX_MENU_TRAIT;
    b->target = followed->id;
    /* Start from the creature's own value, so Enter alone changes nothing. */
    b->value = followed->genome.g[ACTIONS_GIVABLE[b->trait]];
}

static void give(Sandbox *b, Sim *s, char *notice, size_t size)
{
    int gene = ACTIONS_GIVABLE[b->trait];
    char name[16];
    name_from_id(b->target, name, sizeof(name));
    if (actions_give_trait(s, b->target, gene, b->value)) {
        snprintf(notice, size, "gave %s %s %.2f", name,
                 actions_gene_name(gene), (double)b->value);
    } else {
        snprintf(notice, size, "%s is no longer alive", name);
    }
    b->menu = SANDBOX_MENU_NONE;
}

static int trait_key(Sandbox *b, Sim *s, int key, char *notice, size_t size)
{
    int d = digit(key, ACTIONS_GIVABLE_COUNT);
    if (d >= 0) {
        b->trait = d;
        /* The new trait's value as the creature has it now. */
        for (int i = 0; i < s->pool.capacity; i++) {
            const Creature *c = &s->pool.items[i];
            if (c->alive && c->id == b->target) {
                b->value = c->genome.g[ACTIONS_GIVABLE[d]];
            }
        }
    } else if (key == ',' || key == '<') {
        b->value = clamp01(b->value - SANDBOX_VALUE_STEP);
    } else if (key == '.' || key == '>') {
        b->value = clamp01(b->value + SANDBOX_VALUE_STEP);
    } else if (key == '0') {
        b->value = 0.0f;
    } else if (key == '9') {
        b->value = 1.0f;
    } else if (key == KEY_ENTER) {
        give(b, s, notice, size);
    }
    return SANDBOX_TAKEN; /* every key while the menu is open */
}

static int weather_key(Sandbox *b, Sim *s, int key, char *notice, size_t size)
{
    int d = digit(key, WEATHER_COUNT - 1);
    if (d >= 0) {
        int kind = WEATHER_NONE + 1 + d;
        if (actions_weather(s, kind)) {
            snprintf(notice, size, "a %s begins, for one season",
                     weather_name(kind));
        }
        b->menu = SANDBOX_MENU_NONE;
    }
    return SANDBOX_TAKEN;
}

static int skip_key(Sandbox *b, Sim *s, int key, uint64_t now_ms,
                    char *notice, size_t size)
{
    int d = digit(key, 3);
    if (d >= 0) {
        b->menu = SANDBOX_MENU_NONE;
        if (skip_start(&b->skip, s, SKIP_YEARS[d])) {
            b->skip_started_ms = now_ms;
        } else {
            snprintf(notice, size, "nothing to skip: the world has ended");
        }
    }
    return SANDBOX_TAKEN;
}

/* The step nearest to v in a table of n. */
static int nearest(const float *steps, int n, float v)
{
    int best = 0;
    for (int i = 1; i < n; i++) {
        float d = steps[i] - v, best_d = steps[best] - v;
        if (d * d < best_d * best_d) {
            best = i;
        }
    }
    return best;
}

void sandbox_open_setup(Sandbox *b, const Settings *set)
{
    b->menu = SANDBOX_MENU_SETUP;
    b->wild_step = nearest(WILD_STEPS, SANDBOX_WILD_STEPS,
                           set->trait_mutation_scale);
    b->jump_step = nearest(JUMP_STEPS, SANDBOX_JUMP_STEPS, set->jump_chance);
    b->jump_genes = set->jump_genes;
}

void sandbox_setup_apply(const Sandbox *b, Settings *set)
{
    set->trait_mutation_scale = WILD_STEPS[b->wild_step];
    set->jump_chance = JUMP_STEPS[b->jump_step];
    set->jump_genes = b->jump_genes;
}

static int setup_key(Sandbox *b, int key)
{
    int d = digit(key, ACTIONS_GIVABLE_COUNT);
    if (d >= 0) {
        /* ^ is exclusive or: the bit flips, on to off or off to on. 1 << gene
         * is a number with only that gene's bit set. */
        b->jump_genes ^= 1 << ACTIONS_GIVABLE[d];
    } else if ((key == ',' || key == '<') && b->wild_step > 0) {
        b->wild_step--;
    } else if ((key == '.' || key == '>') && b->wild_step < SANDBOX_WILD_STEPS - 1) {
        b->wild_step++;
    } else if (key == '-' && b->jump_step > 0) {
        b->jump_step--;
    } else if ((key == '+' || key == '=') && b->jump_step < SANDBOX_JUMP_STEPS - 1) {
        b->jump_step++;
    } else if (key == KEY_ENTER) {
        b->menu = SANDBOX_MENU_NONE;
        return SANDBOX_RESTART;
    }
    return SANDBOX_TAKEN;
}

int sandbox_key(Sandbox *b, Sim *s, int key, const Creature *followed,
                uint64_t now_ms, char *notice, size_t size)
{
    if (sandbox_skipping(b)) {
        /* While skipping, Escape stops it and nothing else does anything. */
        if (key == SANDBOX_KEY_ESCAPE) {
            skip_cancel(&b->skip);
            b->menu = SANDBOX_MENU_SUMMARY;
        }
        return SANDBOX_TAKEN;
    }
    if (b->menu == SANDBOX_MENU_SUMMARY) {
        b->menu = SANDBOX_MENU_NONE; /* any key closes it */
        return SANDBOX_TAKEN;
    }
    if (b->menu != SANDBOX_MENU_NONE && key == SANDBOX_KEY_ESCAPE) {
        b->menu = SANDBOX_MENU_NONE;
        return SANDBOX_TAKEN;
    }
    switch (b->menu) {
    case SANDBOX_MENU_TRAIT: return trait_key(b, s, key, notice, size);
    case SANDBOX_MENU_WEATHER: return weather_key(b, s, key, notice, size);
    case SANDBOX_MENU_SKIP: return skip_key(b, s, key, now_ms, notice, size);
    case SANDBOX_MENU_SETUP: return setup_key(b, key);
    default: break;
    }
    if (key == 'm' || key == 'M') {
        b->on = !b->on;
        b->menu = SANDBOX_MENU_NONE;
#ifdef AGON_DEMO
        snprintf(notice, size, b->on ? "Sandbox: w weather (the browser demo has "
                                       "only this tool)"
                                     : "Documentary: watching only");
#else
        snprintf(notice, size, b->on ? "Sandbox: g give a trait, w weather, x branch, "
                                       "k skip ahead, u new start"
                                     : "Documentary: watching only");
#endif
        return SANDBOX_TAKEN;
    }
    if (!b->on) {
        return SANDBOX_IGNORED;
    }
#ifdef AGON_DEMO
    if (key == 'g' || key == 'G' || key == 'k' || key == 'K' || key == 'u' ||
        key == 'U') {
        return not_in_demo(notice, size);
    }
#endif
    if (key == 'u' || key == 'U') {
        sandbox_open_setup(b, &s->set);
        return SANDBOX_TAKEN;
    }
    if (key == 'g' || key == 'G') {
        if (followed == NULL) {
            snprintf(notice, size, "click a creature first, then g");
        } else {
            open_trait(b, followed);
        }
        return SANDBOX_TAKEN;
    }
    if (key == 'w' || key == 'W') {
        b->menu = SANDBOX_MENU_WEATHER;
        return SANDBOX_TAKEN;
    }
    if (key == 'k' || key == 'K') {
        b->menu = SANDBOX_MENU_SKIP;
        return SANDBOX_TAKEN;
    }
    return SANDBOX_IGNORED;
}

bool sandbox_skipping(const Sandbox *b)
{
    return b->skip.active;
}

bool sandbox_skip_step(Sandbox *b, Sim *s, uint64_t (*now_ms)(void),
                       uint64_t budget_ms)
{
    if (!b->skip.active) {
        return false;
    }
    if (!skip_step(&b->skip, s, now_ms, budget_ms)) {
        b->menu = SANDBOX_MENU_SUMMARY;
    }
    return b->skip.active;
}

static int setup_lines(const Sandbox *b, char lines[][SKIP_COLS], int max)
{
    int n = 0;
    if (max < ACTIONS_GIVABLE_COUNT + 6) {
        return 0; /* the screen is all or nothing */
    }
    snprintf(lines[n++], SKIP_COLS, "A new start: how bodies change from "
                                    "parent to young");
    snprintf(lines[n++], SKIP_COLS, " wildness  x%.1f  (, and . change it; "
                                    "1 is nature's own)",
             (double)WILD_STEPS[b->wild_step]);
    snprintf(lines[n++], SKIP_COLS, " jumps     %.0f %% of changes  "
                                    "(- and + change it)",
             (double)(JUMP_STEPS[b->jump_step] * 100.0f));
    snprintf(lines[n++], SKIP_COLS, " traits that may jump to any value:");
    for (int i = 0; i < ACTIONS_GIVABLE_COUNT; i++) {
        int gene = ACTIONS_GIVABLE[i];
        snprintf(lines[n++], SKIP_COLS, "   %d [%c] %s", i + 1,
                 (b->jump_genes >> gene & 1) ? 'x' : ' ', actions_gene_name(gene));
    }
    snprintf(lines[n++], SKIP_COLS, "Enter starts this world again from its "
                                    "first tick with these;");
    snprintf(lines[n++], SKIP_COLS, "Esc keeps the world as it is");
    return n;
}

int sandbox_menu_lines(const Sandbox *b, const Sim *s, uint64_t now_ms,
                       char lines[][SKIP_COLS], int max)
{
    int n = 0;
    if (sandbox_skipping(b)) {
        if (max > 0) {
            skip_progress_text(&b->skip, s, now_ms - b->skip_started_ms,
                               lines[n++], SKIP_COLS);
        }
        return n;
    }
    switch (b->menu) {
    case SANDBOX_MENU_TRAIT: {
        char name[16];
        name_from_id(b->target, name, sizeof(name));
        snprintf(lines[n++], SKIP_COLS, "Give %s a trait. Its young inherit it.",
                 name);
        for (int i = 0; i < ACTIONS_GIVABLE_COUNT && n < max - 1; i++) {
            snprintf(lines[n++], SKIP_COLS, " %c %d %s", i == b->trait ? '>' : ' ',
                     i + 1, actions_gene_name(ACTIONS_GIVABLE[i]));
        }
        if (n < max) {
            snprintf(lines[n++], SKIP_COLS,
                     "value %.2f  (, and . change it, 0 and 9 the ends; "
                     "Enter gives, Esc closes)", (double)b->value);
        }
        break;
    }
    case SANDBOX_MENU_WEATHER:
        snprintf(lines[n++], SKIP_COLS, "Weather for one season:");
        for (int k = 1; k < WEATHER_COUNT && n < max; k++) {
            snprintf(lines[n++], SKIP_COLS, "   %d %s", k, weather_name(k));
        }
        if (n < max) {
            snprintf(lines[n++], SKIP_COLS, "Esc closes");
        }
        break;
    case SANDBOX_MENU_SKIP:
        snprintf(lines[n++], SKIP_COLS, "Skip ahead:");
        for (int k = 0; k < 3 && n < max; k++) {
            snprintf(lines[n++], SKIP_COLS, "   %d %d %s", k + 1, SKIP_YEARS[k],
                     SKIP_YEARS[k] == 1 ? "year" : "years");
        }
        if (n < max) {
            snprintf(lines[n++], SKIP_COLS, "20 years takes a minute or two. "
                                            "Esc closes");
        }
        break;
    case SANDBOX_MENU_SUMMARY:
        n = skip_summary_lines(&b->skip, s, lines, max - 1);
        snprintf(lines[n++], SKIP_COLS, "(any key closes)");
        break;
    case SANDBOX_MENU_SETUP:
        n = setup_lines(b, lines, max);
        break;
    default: break;
    }
    return n;
}

const char *sandbox_help(const Sandbox *b)
{
#ifdef AGON_DEMO
    return b->on ? " Sandbox | m mode | w weather | c copy code | o open code"
                 : " Documentary | m mode | c copy code | o open code";
#else
    return b->on ? " Sandbox | m mode | g trait | w weather | k skip | x branch"
                   " | Tab | u new start | c/o code"
                 : " Documentary | m mode | c copy code | o open code";
#endif
}

void sandbox_free(Sandbox *b)
{
    skip_free(&b->skip);
}
