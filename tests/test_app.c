#include "test.h"

#include <string.h> /* strstr */

#include "app.h"
#include "options.h"
#include "settings.h"
#include "sim.h"

/* A small world, built fresh: the prelude tests run whole years of it. */
static void small_world(Sim *s, const Settings *set)
{
    CHECK(sim_init(s, set, 5, 48, 32));
}

static void test_who_gets_a_prelude(void)
{
    Settings set = settings_default();
    set.ancestor_years = 30;
    Options o = {.ancestors = -1};
    CHECK(app_prelude_years(&o, &set, true) == 30);
    CHECK(app_prelude_years(&o, &set, false) == 0); /* --once, --frames */
    o.ancestors = 4;
    CHECK(app_prelude_years(&o, &set, true) == 4);
    CHECK(app_prelude_years(&o, &set, false) == 4);
    o.ancestors = 0;
    CHECK(app_prelude_years(&o, &set, true) == 0);
    o.ancestors = -1;
    o.load_path = "world.save"; /* a saved world has lived already */
    CHECK(app_prelude_years(&o, &set, true) == 0);
    o.ancestors = 7;
    CHECK(app_prelude_years(&o, &set, true) == 0);
}

static void test_prelude_is_the_same_world(void)
{
    /* Two years of prelude reach the same world as 1,200 plain steps. */
    Settings set = settings_default();
    static Sim a, b; /* static: too large for the stack */
    small_world(&a, &set);
    small_world(&b, &set);
    Prelude p;
    app_prelude_start(&p, &a, 2);
    CHECK(p.active);
    app_prelude_finish(&p, &a);
    CHECK(!p.active);
    for (int k = 0; k < 2 * set.ticks_per_year && !sim_is_over(&b); k++) {
        sim_step(&b);
    }
    CHECK(a.tick == b.tick);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    /* No years: no prelude, the world untouched. */
    app_prelude_start(&p, &b, 0);
    CHECK(!p.active);
    sim_free(&a);
    sim_free(&b);
}

/* A clock for the tests: each reading is 1 ms later than the last. */
static uint64_t fake_ms = 0;
static uint64_t fake_clock(void)
{
    return fake_ms++;
}

static void test_prelude_steps_within_its_budget(void)
{
    Settings set = settings_default();
    static Sim s;
    small_world(&s, &set);
    Prelude p;
    app_prelude_start(&p, &s, 1);
    /* Each tick reads the clock once after the start: a 5 ms budget runs 5. */
    CHECK(app_prelude_step(&p, &s, fake_clock, 5));
    CHECK(s.tick == 5);
    char text[128];
    app_prelude_text(&p, &s, text, sizeof(text));
    CHECK(strstr(text, "year 1 of 1") != NULL);
    /* A key ends it where it got to. */
    app_prelude_stop(&p);
    CHECK(!p.active);
    CHECK(!app_prelude_step(&p, &s, fake_clock, 5));
    CHECK(s.tick == 5);
    sim_free(&s);
}

int main(void)
{
    test_who_gets_a_prelude();
    test_prelude_is_the_same_world();
    test_prelude_steps_within_its_budget();
    TEST_SUMMARY();
}
