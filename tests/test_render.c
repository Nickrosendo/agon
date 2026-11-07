#include "test.h"

#include "brain.h"    /* ACT_SLEEP */
#include "creature.h" /* Creature */
#include "palette.h"
#include "render.h"
#include "settings.h"
#include "sim.h"
#include "world.h" /* Plant */

static int same(Colour a, Colour b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

static void test_ground_follows_its_water(void)
{
    Colour desert = palette_ground(0.5f, 0.0f);
    Colour plain = palette_ground(0.5f, 0.6f);
    Colour soaked = palette_ground(0.5f, 1.0f); /* the top must stay in the table */
    CHECK(same(plain, palette_soil(0.5f)));
    CHECK(!same(desert, plain));
    CHECK(!same(soaked, plain));
    /* Lighter when dry, darker when soaked. */
    CHECK(desert.r > plain.r);
    CHECK(soaked.r < plain.r);
}

static void test_display_hue_skips_the_greens(void)
{
    /* Every inherited hue, in steps of 0.001, lands outside the plants'
     * greens, in 0 .. 1. */
    int inside = 0, outside_wheel = 0;
    for (int i = 0; i < 1000; i++) {
        float shown = render_display_hue((float)i / 1000.0f);
        if (shown >= RENDER_PLANT_HUE_FROM && shown < RENDER_PLANT_HUE_TO) {
            inside++;
        }
        if (shown < 0.0f || shown >= 1.0f) {
            outside_wheel++;
        }
    }
    CHECK(inside == 0);
    CHECK(outside_wheel == 0);
}

static void test_display_hue_keeps_the_order(void)
{
    /* Close hues stay close and in the same order, so related families
     * still look related: the wheel is squeezed, not shuffled. */
    float before = render_display_hue(0.0f);
    CHECK(before == 0.0f); /* red stays red */
    for (int i = 1; i < 1000; i++) {
        float shown = render_display_hue((float)i / 1000.0f);
        CHECK(shown > before);
        before = shown;
    }
}

static void test_plant_hues_stay_in_the_greens(void)
{
    /* Every water need gives a hue inside the plants' greens, which creatures
     * never use, the driest and the thirstiest differ, and a need of exactly 1
     * does not reach past the last step. */
    int outside = 0;
    for (int b = 0; b <= 100; b++) {
        float hue = render_plant_hue((float)b / 100.0f);
        if (hue < RENDER_PLANT_HUE_FROM || hue > RENDER_PLANT_HUE_TO) {
            outside++;
        }
    }
    CHECK(outside == 0);
    CHECK(render_plant_hue(0.0f) != render_plant_hue(1.0f));
    /* Close needs share a hue: render_world skips cells that have not changed. */
    CHECK(render_plant_hue(0.5f) == render_plant_hue(0.51f));
}

/* Ripe fruit shows, what a plant offers shows, and huge fruit from a damaged
 * save stays safe. */
static void test_plant_colour_shows_fruit(void)
{
    Plant plain = {.water_need = 0.5f};
    Plant ripe = plain;
    ripe.fruit = 2.0f;
    Colour a = palette_plant(&plain), b = palette_plant(&ripe);
    CHECK(a.r != b.r || a.g != b.g || a.b != b.b);
    Plant huge = plain;
    huge.fruit = 1e30f;
    Plant full = plain;
    full.fruit = 3.0f;
    Colour c = palette_plant(&huge), d = palette_plant(&full);
    CHECK(c.r == d.r && c.g == d.g && c.b == d.b);
    Plant tree = plain;
    tree.fruit_share = 1.0f;
    Colour e = palette_plant(&tree);
    CHECK(a.r != e.r || a.g != e.g || a.b != e.b);
}

/* The ticks of each season in the default 600-tick year: 150 each, spring
 * first (years.h). */
#define TICK_SUMMER 150
#define TICK_AUTUMN 300
#define TICK_WINTER 450

/* A soil cell with no creature on it, cleared to bare, dry-of-everything soil:
 * no plant, carrion, rain or snow. -1 if the world has none (it always has). */
static int plain_soil_cell(Sim *s)
{
    World *w = &s->world;
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->terrain[i] == TERRAIN_SOIL && w->occupant[i] < 0) {
            w->veg[i] = 0;
            w->veg_wither[i] = 0;
            w->carrion[i] = 0.0f;
            w->rain[i] = 0.0f;
            w->snow[i] = 0.0f;
            return i;
        }
    }
    return -1;
}

/* Snow whitens the soil in proportion to how much lies there; water and rock
 * never hold snow, and even a damaged save that gives them some draws them
 * as they are. */
static void test_snow_whitens_ground(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 7, 64, 32));
    s.tick = TICK_WINTER;
    int i = plain_soil_cell(&s);
    CHECK(i >= 0);
    Colour bare = render_cell_colour(&s, i);
    s.world.snow[i] = 1.0f;
    CHECK(same(render_cell_colour(&s, i), PALETTE_SNOW));
    s.world.snow[i] = 0.5f;
    CHECK(same(render_cell_colour(&s, i), palette_mix(bare, PALETTE_SNOW, 0.5f)));
    s.world.snow[i] = 0.0f;
    CHECK(same(render_cell_colour(&s, i), bare));
    s.world.snow[i] = 1.0f;
    s.world.terrain[i] = TERRAIN_WATER;
    CHECK(same(render_cell_colour(&s, i), PALETTE_WATER));
    s.world.terrain[i] = TERRAIN_ROCK;
    CHECK(same(render_cell_colour(&s, i), PALETTE_ROCK));
    sim_free(&s);
}

/* A leaf plant stands bare from autumn to the end of winter; a fruit plant
 * keeps its colour, fruit and all, all year. */
static void test_bare_in_autumn(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 7, 64, 32));
    int i = plain_soil_cell(&s);
    CHECK(i >= 0);
    Plant leaf = {.water_need = 0.5f};
    s.world.plant[i] = leaf;
    s.world.veg[i] = VEG_MAX; /* full density: the cell is the plant's colour */
    s.tick = TICK_AUTUMN;
    CHECK(same(render_cell_colour(&s, i), PALETTE_BARE));
    s.tick = TICK_WINTER;
    CHECK(same(render_cell_colour(&s, i), PALETTE_BARE));
    s.tick = TICK_SUMMER;
    CHECK(same(render_cell_colour(&s, i), palette_plant(&leaf)));
    Plant fruit = {.water_need = 0.5f, .fruit_share = 1.0f, .fruit = 2.0f};
    s.world.plant[i] = fruit;
    s.tick = TICK_WINTER;
    CHECK(same(render_cell_colour(&s, i), palette_plant(&fruit)));
    sim_free(&s);
}

/* A creature in deep sleep is drawn half way between its awake colour and the
 * ground under it. */
static void test_sleeper_drawn_dim(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 7, 64, 32));
    CHECK(s.pool.count > 0);
    Creature *c = &s.pool.items[0];
    World *w = &s.world;
    int i = world_idx(w, c->x, c->y);
    CHECK(w->occupant[i] == 0);
    w->rain[i] = 0.0f;
    c->current.kind = ACT_REST; /* anything but sleep */
    Colour awake = render_cell_colour(&s, i);
    /* The ground under it, seen by lifting the creature off for a moment. */
    w->occupant[i] = -1;
    Colour ground = render_cell_colour(&s, i);
    w->occupant[i] = 0;
    c->current.kind = ACT_SLEEP;
    CHECK(same(render_cell_colour(&s, i), palette_mix(awake, ground, 0.5f)));
    sim_free(&s);
}

int main(void)
{
    test_snow_whitens_ground();
    test_bare_in_autumn();
    test_sleeper_drawn_dim();
    test_ground_follows_its_water();
    test_plant_hues_stay_in_the_greens();
    test_plant_colour_shows_fruit();
    test_display_hue_skips_the_greens();
    test_display_hue_keeps_the_order();
    TEST_SUMMARY();
}
