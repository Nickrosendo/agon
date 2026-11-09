#include "test.h"

#include <stdio.h> /* snprintf */

#include "creature.h"
#include "genome.h"
#include "rng.h"
#include "settings.h"
#include "sim.h"
#include "tactics.h"
#include "world.h"

/* A world of all soil, fertility 1. */
static void make_world(World *w, int width, int height)
{
    CHECK(world_init(w, width, height));
    for (int i = 0; i < width * height; i++) {
        w->terrain[i] = TERRAIN_SOIL;
        w->fertility[i] = 1.0f;
    }
}

/* A genome of its own for each family: random, so two families are strangers,
 * and copied whole into each member, so members are related (kinship 1). */
static Genome family_genome(uint64_t seed, bool hunter, const Settings *s)
{
    Rng r;
    rng_seed(&r, seed);
    Genome g;
    genome_random(&g, &r, s);
    g.g[GENE_MEAT] = hunter ? 1.0f : 0.0f;
    g.g[GENE_LEAF] = hunter ? 0.0f : 1.0f;
    g.g[GENE_FRUIT] = 0.0f;
    return g;
}

static int place(Pool *p, World *w, int x, int y, const Genome *g, uint32_t lineage)
{
    int idx = pool_spawn(p, w, x, y);
    CHECK(idx >= 0);
    p->items[idx].genome = *g;
    p->items[idx].lineage = lineage;
    p->items[idx].health = 1.0f;
    return idx;
}

/* A victim of a family of its own beside (x, y). helper, if not -1, struck it
 * two ticks ago. Then killer kills it, and the body is taken away. */
static void kill_beside(TacticWatch *t, World *w, Pool *p, int killer, int helper,
                        int x, int y, uint64_t seed, const Settings *s)
{
    Genome g = family_genome(seed, false, s);
    int v = place(p, w, x, y, &g, 5000 + (uint32_t)seed);
    if (helper >= 0) {
        p->items[v].recent_hits[0] = (Hit){p->items[helper].id, p->now - 2};
        p->items[v].hits_taken = 1;
    }
    p->items[v].health = 0.0f;
    tactics_note_hit(t, w, p, &p->items[killer], &p->items[v], true, s);
    pool_kill(p, w, v, s, DEATH_WOUNDS);
}

typedef struct {
    Settings s;
    World w;
    Pool p;
    TacticWatch t;
} Scene;

static void scene_open(Scene *sc)
{
    sc->s = settings_default();
    make_world(&sc->w, 40, 40);
    CHECK(pool_init(&sc->p, 64));
    CHECK(tactics_init(&sc->t));
    sc->p.now = 1000;
    sc->p.light = 1.0f;
}

static void scene_close(Scene *sc)
{
    tactics_free(&sc->t);
    pool_free(&sc->p);
    world_free(&sc->w);
}

/* n checks in a row, as the Sim would make them stats_every ticks apart. */
static void checks(Scene *sc, int n)
{
    for (int k = 0; k < n; k++) {
        tactics_check(&sc->t, &sc->w, &sc->p, NULL, &sc->s);
    }
}

static void test_pack_kills_confirm(void)
{
    Scene sc;
    scene_open(&sc);
    /* Family 1: two hunters side by side, each kill helped by the other.
     * Family 2: one hunter far off, killing alone, fewer kills. */
    Genome g1 = family_genome(1, true, &sc.s);
    Genome g2 = family_genome(2, true, &sc.s);
    int a = place(&sc.p, &sc.w, 10, 10, &g1, 1);
    int b = place(&sc.p, &sc.w, 12, 10, &g1, 1);
    int c = place(&sc.p, &sc.w, 30, 30, &g2, 2);
    for (uint64_t k = 0; k < 15; k++) {
        kill_beside(&sc.t, &sc.w, &sc.p, a, b, 11, 10, 100 + k, &sc.s);
    }
    for (uint64_t k = 0; k < 5; k++) {
        kill_beside(&sc.t, &sc.w, &sc.p, c, -1, 31, 30, 200 + k, &sc.s);
    }
    tactics_check(&sc.t, &sc.w, &sc.p, NULL, &sc.s);
    CHECK(!tactics_seen(&sc.t, TACTIC_PACK_HUNT)); /* one check is a blip */
    checks(&sc, TACTIC_HOLD - 1);
    CHECK(tactics_seen(&sc.t, TACTIC_PACK_HUNT));
    CHECK(tactics_lift(&sc.t, TACTIC_PACK_HUNT) >= sc.s.tactic_lift);
    int of_diet = 0;
    CHECK(tactics_families(&sc.t, TACTIC_PACK_HUNT, &of_diet) == 1);
    CHECK(of_diet == 2);
    scene_close(&sc);
}

static void test_night_kills_confirm(void)
{
    Scene sc;
    scene_open(&sc);
    Genome g1 = family_genome(1, true, &sc.s);
    Genome g2 = family_genome(2, true, &sc.s);
    int a = place(&sc.p, &sc.w, 10, 10, &g1, 1);
    int c = place(&sc.p, &sc.w, 30, 30, &g2, 2);
    place(&sc.p, &sc.w, 30, 34, &g2, 2); /* family 2 has two members */
    sc.p.light = 0.1f;
    for (uint64_t k = 0; k < 15; k++) {
        kill_beside(&sc.t, &sc.w, &sc.p, a, -1, 11, 10, 100 + k, &sc.s);
    }
    sc.p.light = 0.9f;
    for (uint64_t k = 0; k < 15; k++) {
        kill_beside(&sc.t, &sc.w, &sc.p, c, -1, 31, 30, 200 + k, &sc.s);
    }
    checks(&sc, TACTIC_HOLD);
    CHECK(tactics_seen(&sc.t, TACTIC_NIGHT_HUNT));
    int of_diet = 0;
    CHECK(tactics_families(&sc.t, TACTIC_NIGHT_HUNT, &of_diet) == 1);
    scene_close(&sc);
}

static void test_too_few_events(void)
{
    Scene sc;
    scene_open(&sc);
    Genome g1 = family_genome(1, true, &sc.s);
    int a = place(&sc.p, &sc.w, 10, 10, &g1, 1);
    sc.p.light = 0.1f;
    for (uint64_t k = 0; k < 9; k++) {
        kill_beside(&sc.t, &sc.w, &sc.p, a, -1, 11, 10, 100 + k, &sc.s);
    }
    checks(&sc, TACTIC_HOLD);
    CHECK(!tactics_seen(&sc.t, TACTIC_NIGHT_HUNT));
    scene_close(&sc);
}

static void test_drive_hits_are_not_counted(void)
{
    Scene sc;
    scene_open(&sc);
    sc.s.territory_drive = 1.0f;
    sc.p.tactics = &sc.t;
    Genome g1 = family_genome(1, false, &sc.s);
    Genome g2 = family_genome(2, false, &sc.s);
    g1.g[GENE_HUE] = 0.0f;
    g2.g[GENE_HUE] = 0.5f;
    g1.g[GENE_TERRITORY] = 1.0f;
    int a = place(&sc.p, &sc.w, 10, 10, &g1, 1);
    int b = place(&sc.p, &sc.w, 10, 9, &g2, 2);
    sc.p.items[a].age = sc.p.items[b].age = (uint32_t)sc.s.mate_age;
    sc.p.items[a].energy = 100.0f;
    sc.p.items[b].health = 1000.0f; /* lives through every hit */
    Rng r;
    rng_seed(&r, 3);
    for (int k = 0; k < 50; k++) {
        creature_act(&sc.w, &sc.p, a, (Action){.kind = ACT_REST, .hold = 1},
                     &sc.s, &r);
    }
    CHECK(sc.p.items[b].hits_taken > 0); /* the drive did strike */
    bool any_row = false;
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        any_row = any_row || sc.t.rows[i].used;
    }
    CHECK(!any_row); /* and the watch heard nothing of it */
    scene_close(&sc);
}

static void test_watch_changes_nothing(void)
{
    Settings on = settings_default();
    Settings off = on;
    off.tactics = 0;
    /* static: two Sims are too large for the stack. */
    static Sim a, b;
    CHECK(sim_init(&a, &on, 1, 64, 48));
    CHECK(sim_init(&b, &off, 1, 64, 48));
    for (int k = 0; k < 600; k++) {
        sim_step(&a);
        sim_step(&b);
    }
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    CHECK(sim_story_checksum(&a) == sim_story_checksum(&b));
    sim_free(&a);
    sim_free(&b);
}

static void test_names(void)
{
    CHECK(tactics_name(TACTIC_NIGHT_HUNT)[0] == 'n');
    CHECK(tactics_name(TACTIC_HUDDLE)[0] == 'h');
    CHECK(tactics_name(TACTIC_COUNT)[0] == '?');
}

int main(void)
{
    test_pack_kills_confirm();
    test_night_kills_confirm();
    test_too_few_events();
    test_drive_hits_are_not_counted();
    test_watch_changes_nothing();
    test_names();
    TEST_SUMMARY();
}
