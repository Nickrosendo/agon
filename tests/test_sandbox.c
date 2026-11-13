#include "test.h"

#include <string.h> /* strstr */

#include "actions.h"
#include "genome.h"
#include "platform.h" /* KEY_ESCAPE, only to check sandbox.c's copy of it */
#include "sandbox.h"
#include "settings.h"
#include "sim.h"
#include "weather.h"

static uint64_t fake_ms(void)
{
    static uint64_t now = 0;
    now += 10;
    return now;
}

static const Creature *first_living(const Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            return &s->pool.items[i];
        }
    }
    return NULL;
}

static int press(Sandbox *b, Sim *s, int key, const Creature *followed)
{
    char notice[128];
    return sandbox_key(b, s, key, followed, 0, notice, sizeof(notice));
}

/* Documentary takes no tool keys; m switches. */
static void test_documentary_ignores_tools(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 2, 64, 32));
    Sandbox b = {0};
    CHECK(press(&b, &s, 'g', first_living(&s)) == SANDBOX_IGNORED);
    CHECK(press(&b, &s, 'w', NULL) == SANDBOX_IGNORED);
    CHECK(press(&b, &s, 'm', NULL) == SANDBOX_TAKEN);
    CHECK(b.on);
    CHECK(press(&b, &s, 'w', NULL) == SANDBOX_TAKEN);
    CHECK(b.menu == SANDBOX_MENU_WEATHER);
    CHECK(press(&b, &s, KEY_ESCAPE, NULL) == SANDBOX_TAKEN);
    CHECK(b.menu == SANDBOX_MENU_NONE);
    /* Keys the tools do not use still reach the window. */
    CHECK(press(&b, &s, ' ', NULL) == SANDBOX_IGNORED);
    CHECK(s.actions.count == 0);
    sandbox_free(&b);
    sim_free(&s);
}

/* g, a trait's number, . up, Enter: one action in the log, for that creature. */
static void test_give_by_keys(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 2, 64, 32));
    Sandbox b = {.on = true};
    const Creature *c = first_living(&s);
    CHECK(press(&b, &s, 'g', NULL) == SANDBOX_TAKEN); /* no one followed */
    CHECK(b.menu == SANDBOX_MENU_NONE);
    press(&b, &s, 'g', c);
    CHECK(b.menu == SANDBOX_MENU_TRAIT);
    press(&b, &s, '2', c); /* armour */
    press(&b, &s, '9', c); /* all the way */
    press(&b, &s, ',', c); /* one step back */
    char lines[SKIP_LINES][SKIP_COLS];
    int n = sandbox_menu_lines(&b, &s, 0, lines, SKIP_LINES);
    CHECK(n == ACTIONS_GIVABLE_COUNT + 2);
    CHECK(strstr(lines[2], "> 2 armour") != NULL);
    press(&b, &s, '\r', c);
    CHECK(b.menu == SANDBOX_MENU_NONE);
    CHECK(s.actions.count == 1);
    CHECK(s.actions.items[0].gene == GENE_ARMOUR);
    CHECK(s.actions.items[0].creature == c->id);
    CHECK(s.actions.items[0].value > 0.94f && s.actions.items[0].value < 0.96f);
    sandbox_free(&b);
    sim_free(&s);
}

static void test_weather_by_keys(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 2, 64, 32));
    Sandbox b = {.on = true};
    press(&b, &s, 'w', NULL);
    press(&b, &s, '1', NULL);
    CHECK(s.actions.count == 1);
    CHECK(s.actions.items[0].weather == WEATHER_DROUGHT);
    sandbox_free(&b);
    sim_free(&s);
}

/* k 1 skips a year in slices; Esc during a skip stops it and shows the
 * summary; any key closes that. */
static void test_skip_by_keys(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 2, 64, 32));
    Sandbox b = {.on = true};
    press(&b, &s, 'k', NULL);
    press(&b, &s, '1', NULL);
    CHECK(sandbox_skipping(&b));
    CHECK(sandbox_skip_step(&b, &s, fake_ms, 25));
    CHECK(press(&b, &s, 'g', NULL) == SANDBOX_TAKEN); /* tools wait */
    CHECK(press(&b, &s, KEY_ESCAPE, NULL) == SANDBOX_TAKEN);
    CHECK(!sandbox_skipping(&b));
    CHECK(b.menu == SANDBOX_MENU_SUMMARY);
    char lines[SKIP_LINES][SKIP_COLS];
    int n = sandbox_menu_lines(&b, &s, 0, lines, SKIP_LINES);
    CHECK(n >= 3 && strstr(lines[0], "Stopped after") != NULL);
    press(&b, &s, 'x', NULL);
    CHECK(b.menu == SANDBOX_MENU_NONE);
    /* And a skip run to its end opens the summary on its own. */
    press(&b, &s, 'k', NULL);
    press(&b, &s, '1', NULL);
    while (sandbox_skip_step(&b, &s, fake_ms, 25)) {
    }
    CHECK(b.menu == SANDBOX_MENU_SUMMARY);
    sandbox_free(&b);
    sim_free(&s);
}

/* The setup screen: opened on a plain world and closed with Enter it changes
 * nothing; its keys move the wildness, the jump chance and the jumping genes;
 * Enter asks for a restart and only then are the settings written. */
static void test_setup_screen(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Sandbox b = {.on = true};
    CHECK(press(&b, &s, 'u', NULL) == SANDBOX_TAKEN);
    CHECK(b.menu == SANDBOX_MENU_SETUP);
    char lines[SKIP_LINES][SKIP_COLS];
    CHECK(sandbox_menu_lines(&b, &s, 0, lines, SKIP_LINES) > 0);
    CHECK(strstr(lines[1], "x1.0") != NULL);
    CHECK(press(&b, &s, '\r', NULL) == SANDBOX_RESTART);
    CHECK(b.menu == SANDBOX_MENU_NONE);
    Settings after = set;
    sandbox_setup_apply(&b, &after);
    CHECK(after.trait_mutation_scale == set.trait_mutation_scale);
    CHECK(after.jump_chance == set.jump_chance);
    CHECK(after.jump_genes == set.jump_genes);

    CHECK(press(&b, &s, 'u', NULL) == SANDBOX_TAKEN);
    CHECK(press(&b, &s, '.', NULL) == SANDBOX_TAKEN); /* wildness x2 */
    CHECK(press(&b, &s, '+', NULL) == SANDBOX_TAKEN); /* jumps 5 % */
    CHECK(press(&b, &s, '2', NULL) == SANDBOX_TAKEN); /* armour may jump */
    CHECK(press(&b, &s, 'm', NULL) == SANDBOX_TAKEN); /* the menu takes m too */
    CHECK(b.on);
    CHECK(s.set.trait_mutation_scale == 1.0f); /* nothing written yet */
    CHECK(press(&b, &s, '\r', NULL) == SANDBOX_RESTART);
    sandbox_setup_apply(&b, &after);
    CHECK(after.trait_mutation_scale == 2.0f);
    CHECK(after.jump_chance == 0.05f);
    CHECK(after.jump_genes == 1 << GENE_ARMOUR);

    /* Esc leaves the world as it is. */
    CHECK(press(&b, &s, 'u', NULL) == SANDBOX_TAKEN);
    CHECK(press(&b, &s, KEY_ESCAPE, NULL) == SANDBOX_TAKEN);
    CHECK(b.menu == SANDBOX_MENU_NONE);
    sandbox_free(&b);
    sim_free(&s);
}

int main(void)
{
    test_setup_screen();
    test_documentary_ignores_tools();
    test_give_by_keys();
    test_weather_by_keys();
    test_skip_by_keys();
    TEST_SUMMARY();
}
