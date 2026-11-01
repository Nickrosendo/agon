#include "test.h"

#include <string.h> /* strcmp */

#include "brain_script.h"
#include "panel.h"
#include "settings.h"
#include "sim.h"

static int formats_as(long n, const char *expected)
{
    char out[32];
    panel_format_count(out, sizeof(out), n);
    return strcmp(out, expected) == 0;
}

static void test_format_count(void)
{
    CHECK(formats_as(0, "0"));
    CHECK(formats_as(7, "7"));
    CHECK(formats_as(999, "999"));
    CHECK(formats_as(1000, "1,000"));
    CHECK(formats_as(12345, "12,345"));
    CHECK(formats_as(1234567, "1,234,567"));
    CHECK(formats_as(-1234, "-1,234"));
}

static void test_format_count_small_buffer(void)
{
    /* Cut short, but always ended with '\0' and never written past the
     * end: the address checker would report a write past out[3]. */
    char out[4];
    panel_format_count(out, sizeof(out), 12345);
    CHECK(strcmp(out, "12,") == 0);
    char one[1];
    panel_format_count(one, sizeof(one), 5);
    CHECK(one[0] == '\0');
}

static void test_counts(void)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "script");
    set.start_creatures = 0;
    set.min_creatures = 0;
    set.veg_start_chance = 0.0f;
    Sim sim;
    CHECK(sim_init(&sim, &set, 7, 16, 16));
    for (int i = 0; i < 16 * 16; i++) {
        sim.world.terrain[i] = TERRAIN_SOIL;
    }
    sim.world.veg[3] = 1;
    sim.world.veg[4] = VEG_MAX;
    sim.world.veg[5] = 2;
    sim.world.veg_wither[5] = 4;
    sim.world.veg_wither[6] = 9; /* bare: not counted as withering */
    CHECK(pool_spawn(&sim.pool, &sim.world, 8, 8) >= 0);
    CHECK(pool_spawn(&sim.pool, &sim.world, 9, 8) >= 0);

    PanelCounts c = panel_count(&sim);
    CHECK(c.creatures == 2);
    CHECK(c.plants == 3);
    CHECK(c.withering == 1);
    sim_free(&sim);
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_format_count();
    test_format_count_small_buffer();
    test_counts();
    TEST_SUMMARY();
}
