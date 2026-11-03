#include "test.h"

#include <math.h>  /* fabsf, isfinite */
#include <stdio.h> /* snprintf */

#include "brain_script.h"
#include "creature.h"
#include "flora.h"
#include "settings.h"
#include "sim.h"
#include "world.h"

/* A world of all soil, fertility 1, no vegetation. */
static void make_world(World *w, int width, int height)
{
    CHECK(world_init(w, width, height));
    for (int i = 0; i < width * height; i++) {
        w->terrain[i] = TERRAIN_SOIL;
        w->fertility[i] = 1.0f;
    }
}

/* pool_spawn, then a standard body: size 1 and full speed (with the
 * default size_min 0.5 and size_max 1.5), sight 1, a plant-eater. Tests
 * that are about one trait change it afterwards. */
static int spawn(Pool *p, World *w, int x, int y)
{
    int idx = pool_spawn(p, w, x, y);
    if (idx >= 0) {
        Genome *g = &p->items[idx].genome;
        g->g[GENE_SIZE] = 0.5f;
        g->g[GENE_SPEED] = 1.0f;
        g->g[GENE_SIGHT] = 0.0f;
        g->g[GENE_PLANT] = 1.0f; /* a plant-eater: full plant energy */
        g->g[GENE_MEAT] = 0.0f;
    }
    return idx;
}

/* Turns a creature from spawn into a hunter: meat digestion above plant. */
static void make_hunter(Creature *c)
{
    c->genome.g[GENE_MEAT] = 1.0f;
    c->genome.g[GENE_PLANT] = 0.0f;
}

static bool near(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

static void test_pool_slots_and_ids(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Pool p;
    CHECK(pool_init(&p, 3));
    int a = spawn(&p, &w, 1, 1);
    int b = spawn(&p, &w, 2, 1);
    int c = spawn(&p, &w, 3, 1);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    CHECK(a != b && b != c && a != c);
    CHECK(spawn(&p, &w, 4, 1) == -1); /* full */
    CHECK(p.count == 3);
    CHECK(w.occupant[world_idx(&w, 2, 1)] == b);

    uint32_t old_id = p.items[b].id;
    pool_kill(&p, &w, b, &s);
    CHECK(p.count == 2);
    CHECK(w.occupant[world_idx(&w, 2, 1)] == -1);
    pool_kill(&p, &w, b, &s); /* killing a free slot changes nothing */
    CHECK(p.count == 2);

    int d = spawn(&p, &w, 5, 5);
    CHECK(d == b);                      /* the slot is reused */
    CHECK(p.items[d].id != old_id);     /* the id is not */
    CHECK(p.items[d].id == 3);
    pool_free(&p);
    world_free(&w);
}

static void test_spawn_needs_free_soil(void)
{
    World w;
    make_world(&w, 8, 8);
    Pool p;
    CHECK(pool_init(&p, 4));
    CHECK(spawn(&p, &w, 3, 3) >= 0);
    CHECK(spawn(&p, &w, 3, 3) == -1); /* occupied */
    w.terrain[world_idx(&w, 4, 4)] = TERRAIN_WATER;
    CHECK(spawn(&p, &w, 4, 4) == -1);
    CHECK(p.count == 1);
    CHECK(p.next_id == 1); /* failed spawns use no id */
    pool_free(&p);
    world_free(&w);
}

static void test_move_wraps(void)
{
    World w;
    make_world(&w, 8, 6);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 4));
    int i = spawn(&p, &w, 0, 0); /* heading north */
    p.items[i].energy = 10.0f;
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    CHECK(p.items[i].x == 0 && p.items[i].y == 5);
    CHECK(w.occupant[world_idx(&w, 0, 0)] == -1);
    CHECK(w.occupant[world_idx(&w, 0, 5)] == i);
    CHECK(fabsf(p.items[i].energy - (10.0f - s.move_cost)) < 1e-6f);
    pool_free(&p);
    world_free(&w);
}

static void test_move_blocked(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 4));
    int i = spawn(&p, &w, 3, 3);
    p.items[i].energy = 10.0f;
    CHECK(spawn(&p, &w, 3, 2) >= 0); /* someone to the north */
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    CHECK(p.items[i].x == 3 && p.items[i].y == 3);
    CHECK(fabsf(p.items[i].energy - (10.0f - s.move_cost)) < 1e-6f);

    /* Turn right to face east, where there is water, then rock. */
    creature_act(&w, &p, i, (Action){ACT_TURN_RIGHT, 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_EAST);
    w.terrain[world_idx(&w, 4, 3)] = TERRAIN_WATER;
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    w.terrain[world_idx(&w, 4, 3)] = TERRAIN_ROCK;
    creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
    CHECK(p.items[i].x == 3 && p.items[i].y == 3);
    CHECK(fabsf(p.items[i].energy - (10.0f - 3 * s.move_cost)) < 1e-5f);
    pool_free(&p);
    world_free(&w);
}

static void test_turns(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = spawn(&p, &w, 3, 3);
    creature_act(&w, &p, i, (Action){ACT_TURN_LEFT, 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_WEST); /* left of north */
    creature_act(&w, &p, i, (Action){ACT_TURN_RIGHT, 1}, &s, &r);
    creature_act(&w, &p, i, (Action){ACT_TURN_RIGHT, 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_EAST);
    pool_free(&p);
    world_free(&w);
}

static void test_bite(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = spawn(&p, &w, 3, 3);
    p.items[i].energy = 10.0f;
    /* Every plant gives the full bite_veg_energy, whatever its rule;
     * test_vigorous_plants_feed_less covers the food cost. */
    s.veg_vigour_food = 0.0f;
    w.veg[world_idx(&w, 3, 2)] = 3; /* ahead, to the north */
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(w.veg[world_idx(&w, 3, 2)] == 2);
    CHECK(fabsf(p.items[i].energy - (10.0f + s.bite_veg_energy)) < 1e-6f);

    /* A plant beside the creature, not ahead, is eaten just the same. */
    w.veg[world_idx(&w, 3, 2)] = 0;
    w.veg[world_idx(&w, 2, 4)] = 1; /* behind, to the left */
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(w.veg[world_idx(&w, 2, 4)] == 0);

    /* With nothing to eat around, a bite gains nothing. */
    float before = p.items[i].energy;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(p.items[i].energy == before);
    pool_free(&p);
    world_free(&w);
}

/* Two grown creatures with plenty of energy, side by side at (3, 3) and
 * (4, 3), each with its own random genome. */
static void adult_pair(World *w, Pool *p, int *a, int *b, const Settings *s,
                       Rng *r)
{
    make_world(w, 8, 8);
    CHECK(pool_init(p, 8));
    *a = spawn(p, w, 3, 3);
    *b = spawn(p, w, 4, 3);
    for (int k = 0; k < 2; k++) {
        Creature *c = &p->items[k == 0 ? *a : *b];
        genome_random(&c->genome, r, s);
        c->genome.g[GENE_SIZE] = 0.5f; /* size 1 */
        c->age = (uint32_t)s->mate_age;
        c->energy = s->mate_threshold + 10.0f;
    }
}

static void test_mate_needs_both(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 1);
    Action mate = {ACT_MATE, 1};

    /* One willing, the other not asked: no child and no energy spent. */
    adult_pair(&w, &p, &a, &b, &s, &r);
    float before = p.items[a].energy;
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    CHECK(p.count == 2);
    CHECK(p.items[a].energy == before);
    CHECK(p.items[a].mate_wish == s.mate_window);

    /* The second chooses inside the window: one child. Upkeep is what counts
     * the wish down, one tick each. */
    creature_upkeep(&p.items[a], &s);
    CHECK(p.items[a].mate_wish == s.mate_window - 1);
    int child = creature_act(&w, &p, b, mate, &s, &r);
    CHECK(child >= 0);
    CHECK(p.count == 3);
    /* A birth clears both wishes. */
    CHECK(p.items[a].mate_wish == 0 && p.items[b].mate_wish == 0);
    pool_free(&p);
    world_free(&w);

    /* The second chooses after the window closed: nothing. */
    adult_pair(&w, &p, &a, &b, &s, &r);
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    for (int t = 0; t < s.mate_window; t++) {
        creature_upkeep(&p.items[a], &s);
    }
    CHECK(p.items[a].mate_wish == 0);
    CHECK(creature_act(&w, &p, b, mate, &s, &r) == -1);
    CHECK(p.count == 2);
    pool_free(&p);
    world_free(&w);
}

static void test_mate_costs_both_parents(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 2);
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.items[a].energy = 100.0f;
    p.items[b].energy = 60.0f;
    Action mate = {ACT_MATE, 1};
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    int child = creature_act(&w, &p, b, mate, &s, &r);
    CHECK(child >= 0);
    CHECK(near(p.items[a].energy, 100.0f * (1.0f - s.mate_share)));
    CHECK(near(p.items[b].energy, 60.0f * (1.0f - s.mate_share)));
    CHECK(near(p.items[child].energy, 160.0f * s.mate_share));
    CHECK(near(p.items[a].energy + p.items[b].energy + p.items[child].energy,
               160.0f));
    /* The child is on a neighbouring cell of its parents, with no age. */
    CHECK(p.items[child].age == 0);
    int dx = p.items[child].x - 3;
    int dy = p.items[child].y - 3;
    CHECK(dx >= -1 && dx <= 2 && dy >= -1 && dy <= 1);
    pool_free(&p);
    world_free(&w);
}

static void test_mate_needs_adults(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 3);
    Action mate = {ACT_MATE, 1};

    /* Too young. */
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.items[a].age = (uint32_t)s.mate_age - 1;
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    CHECK(p.items[a].mate_wish == 0);
    pool_free(&p);
    world_free(&w);

    /* Too little energy for its size: a bigger body needs more. */
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.items[a].genome.g[GENE_SIZE] = 1.0f; /* size 1.5 */
    p.items[a].energy = s.mate_threshold * 1.25f;
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    CHECK(p.items[a].mate_wish == 0);
    pool_free(&p);
    world_free(&w);

    /* A partner that has wished but has since spent its energy is no use. */
    adult_pair(&w, &p, &a, &b, &s, &r);
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    p.items[a].energy = 1.0f;
    CHECK(creature_act(&w, &p, b, mate, &s, &r) == -1);
    CHECK(p.count == 2);
    pool_free(&p);
    world_free(&w);
}

static void test_child_surname_from_a_parent(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 4);
    int from_a = 0;
    int from_b = 0;
    int neither = 0;
    Action mate = {ACT_MATE, 1};
    for (int k = 0; k < 100; k++) {
        adult_pair(&w, &p, &a, &b, &s, &r);
        p.items[a].level = 2.0f;
        p.items[b].level = 3.0f;
        creature_act(&w, &p, a, mate, &s, &r);
        int child = creature_act(&w, &p, b, mate, &s, &r);
        CHECK(child >= 0);
        CHECK(near(p.items[child].level, 2.5f)); /* the mean of its parents */
        uint32_t name = p.items[child].lineage;
        if (name == p.items[a].lineage) {
            from_a++;
        } else if (name == p.items[b].lineage) {
            from_b++;
        } else {
            neither++;
        }
        pool_free(&p);
        world_free(&w);
    }
    CHECK(neither == 0);
    CHECK(from_a > 0 && from_b > 0);
}

/* Water everywhere except the two parents' own cells. */
static void drown_around_pair(World *w)
{
    for (int i = 0; i < w->w * w->h; i++) {
        w->terrain[i] = TERRAIN_WATER;
    }
    w->terrain[world_idx(w, 3, 3)] = TERRAIN_SOIL;
    w->terrain[world_idx(w, 4, 3)] = TERRAIN_SOIL;
}

static void test_mate_no_room(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 5);
    adult_pair(&w, &p, &a, &b, &s, &r);
    drown_around_pair(&w);
    float before_a = p.items[a].energy;
    float before_b = p.items[b].energy;
    Action mate = {ACT_MATE, 1};
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    CHECK(creature_act(&w, &p, b, mate, &s, &r) == -1);
    CHECK(p.count == 2);
    CHECK(p.items[a].energy == before_a && p.items[b].energy == before_b);
    CHECK(p.births_refused == 0); /* no room is not a full pool */

    /* Room only beside the partner: the child goes there. */
    w.terrain[world_idx(&w, 5, 3)] = TERRAIN_SOIL;
    int child = creature_act(&w, &p, a, mate, &s, &r);
    CHECK(child >= 0);
    CHECK(child >= 0 && p.items[child].x == 5 && p.items[child].y == 3);
    pool_free(&p);
    world_free(&w);
}

static void test_pool_full_counts_refused(void)
{
    World w;
    Pool p;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 6);
    make_world(&w, 8, 8);
    CHECK(pool_init(&p, 2));
    int a = spawn(&p, &w, 3, 3);
    int b = spawn(&p, &w, 4, 3);
    for (int k = 0; k < 2; k++) {
        p.items[k].age = (uint32_t)s.mate_age;
        p.items[k].energy = 100.0f;
    }
    Action mate = {ACT_MATE, 1};
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    CHECK(p.births_refused == 0);
    CHECK(creature_act(&w, &p, b, mate, &s, &r) == -1);
    CHECK(p.births_refused == 1);
    CHECK(p.count == 2);
    CHECK(p.items[a].energy == 100.0f && p.items[b].energy == 100.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_senses_ready_and_alike(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 7);
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.items[a].heading = HEADING_EAST; /* looking straight at b */
    p.items[b].mate_wish = 3;
    Senses in;
    creature_sense(&w, &p, a, &s, &in);
    const float *ahead = &in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES];
    CHECK(ahead[RAY_READY] == 1.0f);
    CHECK(ahead[RAY_ALIKE] ==
          genome_body_likeness(&p.items[a].genome, &p.items[b].genome));
    /* Not wishing: not ready, however alike. */
    p.items[b].mate_wish = 0;
    creature_sense(&w, &p, a, &s, &in);
    CHECK(ahead[RAY_READY] == 0.0f);
    /* No creature on that ray: both 0. */
    const float *left = &in.v[SENSE_RAYS + RAY_LEFT * RAY_VALUES];
    CHECK(left[RAY_READY] == 0.0f && left[RAY_ALIKE] == 0.0f);
    pool_free(&p);
    world_free(&w);
}

/* In season a fed adult is willing with no choice made: whatever it does, it
 * wishes, and two such neighbours have a child. */
static void test_season_drives_fed_adults(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 8);
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.in_season = true;
    Action rest = {ACT_REST, 1};
    CHECK(creature_act(&w, &p, a, rest, &s, &r) == -1);
    CHECK(p.items[a].mate_wish == s.mate_window);
    int child = creature_act(&w, &p, b, rest, &s, &r);
    CHECK(child >= 0);
    CHECK(p.count == 3);
    pool_free(&p);
    world_free(&w);

    /* The season does not excuse being young or hungry. */
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.in_season = true;
    p.items[a].age = (uint32_t)s.mate_age - 1;
    p.items[b].energy = s.mate_threshold / 2.0f;
    CHECK(creature_act(&w, &p, a, rest, &s, &r) == -1);
    CHECK(creature_act(&w, &p, b, rest, &s, &r) == -1);
    CHECK(p.items[a].mate_wish == 0 && p.items[b].mate_wish == 0);
    CHECK(p.count == 2);
    pool_free(&p);
    world_free(&w);
}

/* A willing creature still does what its brain said: a season must not stop
 * anyone eating or walking towards a mate. */
static void test_season_leaves_the_action(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 9);
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.in_season = true;
    p.items[b].energy = 1.0f; /* the neighbour is no partner */
    p.items[a].heading = HEADING_NORTH;
    p.items[a].move_points = 1.0f;
    CHECK(creature_act(&w, &p, a, (Action){ACT_MOVE, 1}, &s, &r) == -1);
    CHECK(p.items[a].y == 2);
    CHECK(p.items[a].mate_wish == s.mate_window);
    pool_free(&p);
    world_free(&w);
}

/* Out of season nothing is automatic: resting never starts a wish, and a child
 * still needs both to choose. */
static void test_out_of_season_needs_a_choice(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 10);
    adult_pair(&w, &p, &a, &b, &s, &r);
    p.in_season = false;
    Action rest = {ACT_REST, 1};
    Action mate = {ACT_MATE, 1};
    CHECK(creature_act(&w, &p, a, rest, &s, &r) == -1);
    CHECK(creature_act(&w, &p, b, rest, &s, &r) == -1);
    CHECK(p.items[a].mate_wish == 0 && p.items[b].mate_wish == 0);
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    CHECK(creature_act(&w, &p, b, mate, &s, &r) >= 0);
    pool_free(&p);
    world_free(&w);
}

static void test_senses_season(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 11);
    adult_pair(&w, &p, &a, &b, &s, &r);
    Senses in;
    p.in_season = false;
    creature_sense(&w, &p, a, &s, &in);
    CHECK(in.v[SENSE_SEASON] == 0.0f);
    p.in_season = true;
    creature_sense(&w, &p, a, &s, &in);
    CHECK(in.v[SENSE_SEASON] == 1.0f);
    pool_free(&p);
    world_free(&w);
}

/* What is one step ahead, in the way the creature faces: the sense a creature
 * needs in order to stop walking into a wall. */
static void test_senses_blocked(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Pool p;
    CHECK(pool_init(&p, 2));
    int i = spawn(&p, &w, 3, 3);
    Senses in;
    int ahead = world_idx(&w, 3, 2); /* heading north */

    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_BLOCKED] == 0.0f);

    w.terrain[ahead] = TERRAIN_WATER;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_BLOCKED] == 1.0f);
    w.terrain[ahead] = TERRAIN_ROCK;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_BLOCKED] == 1.0f);

    /* Turned away from it, the way is clear. */
    p.items[i].heading = HEADING_EAST;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_BLOCKED] == 0.0f);
    p.items[i].heading = HEADING_NORTH;

    /* A neighbour in the way counts too. */
    w.terrain[ahead] = TERRAIN_SOIL;
    int other = spawn(&p, &w, 3, 2);
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_BLOCKED] == 1.0f);
    pool_kill(&p, &w, other, &s);
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_BLOCKED] == 0.0f);

    /* Across the world's edge, as walking does. */
    pool_free(&p);
    CHECK(pool_init(&p, 1));
    world_free(&w);
    make_world(&w, 8, 8);
    i = spawn(&p, &w, 3, 0);
    w.terrain[world_idx(&w, 3, 7)] = TERRAIN_WATER;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_BLOCKED] == 1.0f);
    pool_free(&p);
    world_free(&w);
}

/* How far along a ray the nearest plant is, within sight. */
static void test_senses_plant_along_ray(void)
{
    World w;
    make_world(&w, 32, 32);
    Settings s = settings_default();
    Pool p;
    CHECK(pool_init(&p, 2));
    int i = spawn(&p, &w, 16, 16); /* heading north */
    p.items[i].genome.g[GENE_SIGHT] = 1.0f;
    int sight = creature_sight(&p.items[i], &s);
    CHECK(sight == s.sight_max);
    Senses in;
    const float *ahead = &in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES];
    const float *left = &in.v[SENSE_RAYS + RAY_LEFT * RAY_VALUES];

    creature_sense(&w, &p, i, &s, &in);
    CHECK(ahead[RAY_PLANT] == 0.0f); /* nothing to see */

    /* Three cells ahead: seen, closer reads higher, and the first-cell value
     * stays 0, since it is not within a bite. */
    w.veg[world_idx(&w, 16, 13)] = 1;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(near(ahead[RAY_PLANT], 1.0f - 2.0f / (float)sight));
    CHECK(ahead[RAY_VEG] == 0.0f);
    CHECK(left[RAY_PLANT] == 0.0f); /* the other rays see nothing */

    /* The nearest of two counts. */
    w.veg[world_idx(&w, 16, 15)] = 1;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(near(ahead[RAY_PLANT], 1.0f));
    w.veg[world_idx(&w, 16, 15)] = 0;

    /* Water does not block the view; rock does. */
    w.terrain[world_idx(&w, 16, 14)] = TERRAIN_WATER;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(ahead[RAY_PLANT] > 0.0f);
    w.terrain[world_idx(&w, 16, 14)] = TERRAIN_ROCK;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(ahead[RAY_PLANT] == 0.0f);
    w.terrain[world_idx(&w, 16, 14)] = TERRAIN_SOIL;

    /* Neither does it see past a creature, but it sees up to one. */
    int other = spawn(&p, &w, 16, 14);
    creature_sense(&w, &p, i, &s, &in);
    CHECK(ahead[RAY_PLANT] == 0.0f);
    CHECK(ahead[RAY_NEAR] > 0.0f);
    pool_kill(&p, &w, other, &s);

    /* Past its sight, and with poor sight: unseen. */
    w.veg[world_idx(&w, 16, 13)] = 0;
    w.veg[world_idx(&w, 16, 16 - sight - 1)] = 1;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(ahead[RAY_PLANT] == 0.0f);
    w.veg[world_idx(&w, 16, 16 - sight - 1)] = 0;
    w.veg[world_idx(&w, 16, 13)] = 1;
    p.items[i].genome.g[GENE_SIGHT] = 0.0f; /* sight 1 */
    creature_sense(&w, &p, i, &s, &in);
    CHECK(ahead[RAY_PLANT] == 0.0f);
    pool_free(&p);
    world_free(&w);
}

/* A bite that finds nothing costs energy; one that finds food does not. */
static void test_empty_bite_costs(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    s.bite_empty_cost = 0.25f;
    s.veg_vigour_food = 0.0f;
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = spawn(&p, &w, 3, 3);
    p.items[i].energy = 10.0f;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(near(p.items[i].energy, 9.75f));

    w.veg[world_idx(&w, 3, 2)] = 3;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(near(p.items[i].energy, 9.75f + s.bite_veg_energy));

    /* Off is off. */
    w.veg[world_idx(&w, 3, 2)] = 0;
    s.bite_empty_cost = 0.0f;
    float before = p.items[i].energy;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(near(p.items[i].energy, before));
    pool_free(&p);
    world_free(&w);
}

static void test_plant_eater_cap(void)
{
    World w;
    Pool p;
    int a, b;
    Settings s = settings_default();
    /* Plant-eaters told by their genes; test_cap_by_level covers the
     * count by food-chain level. */
    s.cap_by_level = 0;
    Rng r;
    rng_seed(&r, 1);
    adult_pair(&w, &p, &a, &b, &s, &r);
    for (int k = 0; k < 2; k++) {
        p.items[k].genome.g[GENE_PLANT] = 1.0f;
        p.items[k].genome.g[GENE_MEAT] = 0.0f;
        p.items[k].energy = 100.0f;
    }
    pool_count_plant_eaters(&p, &s);
    CHECK(p.plant_eaters == 2);
    Action mate = {ACT_MATE, 1};

    /* Two alive and a cap of 2: no birth. */
    s.max_plant_eaters = 2;
    CHECK(creature_act(&w, &p, a, mate, &s, &r) == -1);
    CHECK(creature_act(&w, &p, b, mate, &s, &r) == -1);
    CHECK(p.count == 2);

    /* A cap of 3: one birth, which the count follows at once. */
    s.max_plant_eaters = 3;
    CHECK(creature_act(&w, &p, a, mate, &s, &r) >= 0);
    CHECK(p.plant_eaters == 3);

    /* 0 means no cap. */
    s.max_plant_eaters = 0;
    p.items[a].energy = 100.0f;
    p.items[b].energy = 100.0f;
    creature_act(&w, &p, b, mate, &s, &r);
    CHECK(creature_act(&w, &p, a, mate, &s, &r) >= 0);
    pool_free(&p);
    world_free(&w);
}

static void test_vigorous_plants_feed_less(void)
{
    /* veg_vigour_food 0.5: a bite of the most aggressive rule (vigour 1)
     * gives half of bite_veg_energy, one of the most modest (vigour 0)
     * all of it. */
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    s.veg_vigour_food = 0.5f;
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = spawn(&p, &w, 3, 3);
    int ahead = world_idx(&w, 3, 2);

    p.items[i].energy = 10.0f;
    w.veg[ahead] = 1;
    w.veg_rule[ahead] = veg_rule_pack(1, 4, 7);
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(fabsf(p.items[i].energy - (10.0f + 0.5f * s.bite_veg_energy)) <
          1e-5f);

    p.items[i].energy = 10.0f;
    w.veg[ahead] = 1;
    w.veg_rule[ahead] = veg_rule_pack(4, 4, 0);
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(fabsf(p.items[i].energy - (10.0f + s.bite_veg_energy)) < 1e-5f);

    /* Given the choice, a bite takes the more nourishing plant. */
    int behind = world_idx(&w, 3, 4);
    w.veg[ahead] = 1;
    w.veg_rule[ahead] = veg_rule_pack(1, 4, 7);
    w.veg[behind] = 1;
    w.veg_rule[behind] = veg_rule_pack(4, 4, 0);
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(w.veg[behind] == 0 && w.veg[ahead] == 1);
    pool_free(&p);
    world_free(&w);
}

static void test_cap_by_level(void)
{
    /* Two creatures that live on plants (level 2), one with hunter
     * genes. By genes only one counts as a plant-eater; by level both
     * do. A third with plant genes that has eaten meat (level 3) counts
     * by genes but not by level. */
    World w;
    make_world(&w, 12, 12);
    Settings s = settings_default();
    Pool p;
    CHECK(pool_init(&p, 8));
    int grazer = spawn(&p, &w, 1, 1);
    int grazing_hunter = spawn(&p, &w, 5, 5);
    int meat_grazer = spawn(&p, &w, 9, 9);
    make_hunter(&p.items[grazing_hunter]);
    p.items[grazer].level = 2.0f;
    p.items[grazing_hunter].level = 2.0f;
    p.items[meat_grazer].level = 3.0f;

    s.cap_by_level = 0;
    pool_count_plant_eaters(&p, &s);
    CHECK(p.plant_eaters == 2); /* grazer and meat_grazer */
    CHECK(creature_counts_as_plant_eater(&p.items[meat_grazer], &s));
    CHECK(!creature_counts_as_plant_eater(&p.items[grazing_hunter], &s));

    s.cap_by_level = 1;
    pool_count_plant_eaters(&p, &s);
    CHECK(p.plant_eaters == 2); /* grazer and grazing_hunter */
    CHECK(!creature_counts_as_plant_eater(&p.items[meat_grazer], &s));
    CHECK(creature_counts_as_plant_eater(&p.items[grazing_hunter], &s));

    pool_free(&p);
    world_free(&w);
}

static void test_sense(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = spawn(&p, &w, 3, 3); /* heading north */
    p.items[i].energy = s.mate_threshold / 2.0f;
    p.items[i].age = (uint32_t)s.sense_age_half;
    w.veg[world_idx(&w, 3, 2)] = VEG_MAX; /* ahead */
    w.veg[world_idx(&w, 2, 3)] = 2;       /* left: west */
    w.veg[world_idx(&w, 4, 3)] = 0;       /* right: east */
    Senses in;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(fabsf(in.v[SENSE_ENERGY] - 0.5f) < 1e-6f);
    CHECK(fabsf(in.v[SENSE_AGE] - 0.5f) < 1e-6f);
    CHECK(in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES + RAY_VEG] == 1.0f);
    CHECK(in.v[SENSE_RAYS + RAY_LEFT * RAY_VALUES + RAY_VEG] == 0.5f);
    CHECK(in.v[SENSE_RAYS + RAY_RIGHT * RAY_VALUES + RAY_VEG] == 0.0f);
    CHECK(in.v[SENSE_SIZE] == 0.5f); /* the gene spawn set */
    CHECK(in.v[SENSE_DIET] == 0.0f);
    pool_free(&p);
    world_free(&w);
}

/* A Sim using the scripted brain, with no creatures and no floor, whose
 * world is then turned into bare soil everywhere: whatever happens in the
 * test comes from the creatures it places itself. */
static void make_sim(Sim *sim, Settings *set)
{
    *set = settings_default();
    snprintf(set->brain_name, sizeof(set->brain_name), "script");
    set->start_share = 0.0f;
    set->min_creatures = 0;
    set->veg_start_chance = 0.0f;
    CHECK(sim_init(sim, set, 7, 16, 16));
    for (int i = 0; i < 16 * 16; i++) {
        sim->world.terrain[i] = TERRAIN_SOIL;
        sim->world.veg[i] = 0;
    }
}

static void test_level(void)
{
    Creature c = {.level = LEVEL_START};
    creature_ate(&c, 10.0f, 1.0f);
    CHECK(near(c.level, 2.0f));
    creature_ate(&c, 10.0f, 2.0f);
    CHECK(near(c.level, 2.5f));
    creature_ate(&c, 20.0f, 2.5f);
    CHECK(near(c.level, 3.0f));
    CHECK(near(c.eaten, 40.0f));
    creature_ate(&c, 0.0f, 9.0f); /* nothing eaten: nothing changes */
    CHECK(near(c.level, 3.0f));
}

static void test_bite_carrion(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    s.bite_meat_energy = 8.0f;
    s.digest_floor = 0.0f; /* the pure curves; the floor is tested below */
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 2));
    int i = spawn(&p, &w, 3, 3); /* heading north: (3, 2) is ahead */
    p.items[i].genome.g[GENE_MEAT] = 1.0f; /* meat efficiency 1 */
    p.items[i].genome.g[GENE_PLANT] = 0.0f;
    p.items[i].energy = 10.0f;
    int ahead = world_idx(&w, 3, 2);
    carrion_add(&w, ahead, 5.0f, 2.0f);
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(near(p.items[i].energy, 15.0f));
    CHECK(w.carrion[ahead] == 0.0f);
    CHECK(near(p.items[i].level, 3.0f)); /* 1 + level of the meal */

    carrion_add(&w, ahead, 20.0f, 2.0f);
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(near(p.items[i].energy, 23.0f)); /* at most one bite of 8 */
    CHECK(near(w.carrion[ahead], 12.0f));

    /* Both carrion and plants ahead: a plant-eater eats the plants. */
    p.items[i].genome.g[GENE_MEAT] = 0.0f;
    p.items[i].genome.g[GENE_PLANT] = 1.0f;
    w.veg[ahead] = 2;
    float carrion_before = w.carrion[ahead];
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(w.veg[ahead] == 1);
    CHECK(w.carrion[ahead] == carrion_before);
    /* A meat-eater, the carrion. */
    p.items[i].genome.g[GENE_MEAT] = 1.0f;
    p.items[i].genome.g[GENE_PLANT] = 0.0f;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(w.veg[ahead] == 1);
    CHECK(w.carrion[ahead] < carrion_before);
    w.veg[ahead] = 0;

    /* A pure plant-eater gets nothing from meat, and its level stays. */
    p.items[i].genome.g[GENE_MEAT] = 0.0f;
    p.items[i].genome.g[GENE_PLANT] = 1.0f;
    float before = p.items[i].energy;
    float level_before = p.items[i].level;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(near(p.items[i].energy, before));
    CHECK(p.items[i].level == level_before);

    /* With a floor of 0.25, the same plant-eater gets a quarter of what
     * it takes, and its level moves towards the carrion's. */
    s.digest_floor = 0.25f;
    carrion_add(&w, ahead, 20.0f, 4.0f); /* a full bite of 8 */
    before = p.items[i].energy;
    creature_act(&w, &p, i, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(near(p.items[i].energy, before + 0.25f * 8.0f));
    CHECK(p.items[i].level != level_before);
    pool_free(&p);
    world_free(&w);
}

static void test_attack_chance(void)
{
    CHECK(near(attack_chance(1.0f, 1.0f, 3.0f), 0.5f));
    CHECK(near(attack_chance(2.0f, 1.0f, 3.0f), 0.8889f));
    CHECK(near(attack_chance(1.0f, 2.0f, 3.0f), 0.1111f));
}

static void test_attacks_hit_half_the_time(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 7);
    Pool p;
    CHECK(pool_init(&p, 2));
    int a = spawn(&p, &w, 3, 3);
    int t = spawn(&p, &w, 3, 2); /* right ahead, the same size */
    /* Only hunters attack: the attacker digests meat better than plants. */
    p.items[a].genome.g[GENE_MEAT] = 1.0f;
    p.items[a].genome.g[GENE_PLANT] = 0.0f;
    p.items[a].energy = 1e6f;
    int hits = 0, misses_checked = 0;
    for (int k = 0; k < 10000; k++) {
        p.items[t].energy = 1000.0f;
        float attacker_before = p.items[a].energy;
        creature_act(&w, &p, a, (Action){ACT_BITE, 1}, &s, &r);
        if (p.items[t].energy < 1000.0f) {
            hits++;
            CHECK(near(p.items[t].energy, 1000.0f - s.bite_damage * 1.0f));
        } else if (misses_checked < 10) {
            /* A miss still costs the attacker; the target is untouched. */
            CHECK(p.items[t].energy == 1000.0f);
            misses_checked++;
        }
        /* Every attack costs the same, hit or miss. The tolerance is
         * relative: at a million, a float's steps are 0.0625 apart. */
        CHECK(fabsf(attacker_before - p.items[a].energy - s.attack_cost) <
              0.1f);
    }
    CHECK(hits >= 4800 && hits <= 5200);
    CHECK(misses_checked == 10);
    pool_free(&p);
    world_free(&w);
}

/* Two creatures side by side, the first heading north with the second
 * ahead of it, on an 8 by 8 world of bare soil. */
static void two_neighbours(World *w, Pool *p, int *a, int *t)
{
    make_world(w, 8, 8);
    CHECK(pool_init(p, 2));
    *a = spawn(p, w, 3, 3);
    *t = spawn(p, w, 3, 2);
    p->items[*a].energy = 100.0f;
    p->items[*t].energy = 100.0f;
}

static void test_who_attacks(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 3);
    World w;
    Pool p;
    int a, t;

    /* A plant-eater never attacks: its bite does nothing here. */
    two_neighbours(&w, &p, &a, &t);
    for (int k = 0; k < 20; k++) {
        creature_act(&w, &p, a, (Action){ACT_BITE, 1}, &s, &r);
    }
    CHECK(p.items[t].energy == 100.0f);
    CHECK(p.items[a].energy == 100.0f);
    pool_free(&p);
    world_free(&w);

    /* A hunter with no carrion around attacks the plant-eater. */
    two_neighbours(&w, &p, &a, &t);
    make_hunter(&p.items[a]);
    for (int k = 0; k < 20; k++) {
        creature_act(&w, &p, a, (Action){ACT_BITE, 1}, &s, &r);
    }
    CHECK(p.items[t].energy < 100.0f);
    pool_free(&p);
    world_free(&w);

    /* With carrion beside it, the hunter eats that instead. */
    two_neighbours(&w, &p, &a, &t);
    make_hunter(&p.items[a]);
    int beside = world_idx(&w, 4, 3); /* east */
    carrion_add(&w, beside, 20.0f, 2.0f);
    creature_act(&w, &p, a, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(p.items[t].energy == 100.0f);
    CHECK(w.carrion[beside] < 20.0f);
    pool_free(&p);
    world_free(&w);

    /* Hunters leave each other alone. */
    two_neighbours(&w, &p, &a, &t);
    make_hunter(&p.items[a]);
    make_hunter(&p.items[t]);
    for (int k = 0; k < 20; k++) {
        creature_act(&w, &p, a, (Action){ACT_BITE, 1}, &s, &r);
    }
    CHECK(p.items[t].energy == 100.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_food_under_a_creature(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 3);
    World w;
    Pool p;
    int a, t;
    two_neighbours(&w, &p, &a, &t);
    w.veg[world_idx(&w, 3, 2)] = 3; /* under t: covered */
    creature_act(&w, &p, a, (Action){ACT_BITE, 1}, &s, &r);
    CHECK(w.veg[world_idx(&w, 3, 2)] == 3);
    CHECK(p.items[a].energy == 100.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_slow_creature(void)
{
    World w;
    make_world(&w, 8, 8);
    Settings s = settings_default();
    s.speed_min = 0.25f;
    Rng r;
    rng_seed(&r, 1);
    Pool p;
    CHECK(pool_init(&p, 1));
    int i = spawn(&p, &w, 3, 7);
    p.items[i].genome.g[GENE_SPEED] = 0.0f; /* speed 0.25 */
    p.items[i].energy = 100.0f;
    int moves = 0, y = p.items[i].y;
    for (int t = 0; t < 8; t++) {
        creature_act(&w, &p, i, (Action){ACT_MOVE, 1}, &s, &r);
        if (p.items[i].y != y) {
            moves++;
            y = p.items[i].y;
        }
    }
    CHECK(moves == 2); /* once in four ticks */
    pool_free(&p);
    world_free(&w);
}

static void test_ray_sees_creature(void)
{
    World w;
    make_world(&w, 12, 12);
    Settings s = settings_default();
    s.sight_max = 4;
    Pool p;
    CHECK(pool_init(&p, 2));
    int i = spawn(&p, &w, 5, 8);
    p.items[i].genome.g[GENE_SIGHT] = 1.0f; /* sight 4 */
    int j = spawn(&p, &w, 5, 6); /* two cells ahead (north) */
    p.items[j].genome.g[GENE_HUE] = 0.3f; /* not identical to the viewer */
    Senses in;
    creature_sense(&w, &p, i, &s, &in);
    const float *ahead = &in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES];
    CHECK(near(ahead[RAY_NEAR], 0.75f));
    CHECK(near(ahead[RAY_RELSIZE], 0.5f));
    CHECK(ahead[RAY_KIN] ==
          genome_kinship(&p.items[i].genome, &p.items[j].genome));
    CHECK(ahead[RAY_HUNTER] == 0.0f); /* a plant-eater */
    make_hunter(&p.items[j]);
    creature_sense(&w, &p, i, &s, &in);
    CHECK(ahead[RAY_HUNTER] == 1.0f);
    /* Nothing to the sides. */
    CHECK(in.v[SENSE_RAYS + RAY_LEFT * RAY_VALUES + RAY_NEAR] == 0.0f);
    /* Carrion three cells to the left (west). After its scent has
     * spread for a while, the left ray smells more than the right. */
    carrion_add(&w, world_idx(&w, 2, 8), 40.0f, 2.0f);
    for (int t = 0; t < 30; t++) {
        scent_step(&w, &s);
    }
    creature_sense(&w, &p, i, &s, &in);
    float left = in.v[SENSE_RAYS + RAY_LEFT * RAY_VALUES + RAY_CARRION];
    float right = in.v[SENSE_RAYS + RAY_RIGHT * RAY_VALUES + RAY_CARRION];
    CHECK(left > 0.0f && left < 1.0f);
    CHECK(left > right);
    pool_free(&p);
    world_free(&w);
}

static void test_ray_limits(void)
{
    World w;
    make_world(&w, 16, 16);
    Settings s = settings_default();
    s.sight_max = 4;
    Pool p;
    CHECK(pool_init(&p, 3));
    int i = spawn(&p, &w, 5, 10);
    p.items[i].genome.g[GENE_SIGHT] = 1.0f; /* sight 4 */
    spawn(&p, &w, 5, 5); /* five cells ahead: out of sight */
    Senses in;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES + RAY_NEAR] == 0.0f);

    /* Rock between the viewer and a creature two cells ahead. */
    spawn(&p, &w, 5, 8);
    w.terrain[world_idx(&w, 5, 9)] = TERRAIN_ROCK;
    creature_sense(&w, &p, i, &s, &in);
    CHECK(in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES + RAY_NEAR] == 0.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_starved_creature_dies(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int i = spawn(&sim.pool, &sim.world, 4, 4);
    sim.pool.items[i].energy = set.upkeep_base / 2.0f;
    sim_step(&sim);
    CHECK(!sim.pool.items[i].alive);
    CHECK(sim.pool.count == 0);
    CHECK(sim.world.occupant[world_idx(&sim.world, 4, 4)] == -1);
    CHECK(brain_script_deaths(&sim.brain) == 1);
    /* The body is left as carrion, at the creature's level. Energy left
     * was below 0, so only the body counts. */
    int cell = world_idx(&sim.world, 4, 4);
    CHECK(near(sim.world.carrion[cell], set.body_energy * 1.0f));
    CHECK(near(sim.world.carrion_level[cell], LEVEL_START));
    sim_free(&sim);
}

static void test_killed_creature_leaves_carrion(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.attack_steepness = 50.0f; /* the bigger one always hits */
    int a = spawn(&sim.pool, &sim.world, 4, 6);
    int t = spawn(&sim.pool, &sim.world, 4, 5); /* ahead of a */
    sim.pool.items[a].genome.g[GENE_SIZE] = 1.0f; /* size 1.5 */
    sim.pool.items[a].genome.g[GENE_MEAT] = 1.0f; /* a hunter */
    sim.pool.items[a].genome.g[GENE_PLANT] = 0.0f;
    sim.pool.items[t].genome.g[GENE_SIZE] = 0.0f; /* size 0.5 */
    sim.pool.items[a].energy = 100.0f;
    sim.pool.items[t].energy = 1.0f; /* one hit is enough */
    sim.pool.items[t].level = 2.5f;
    uint32_t target_id = sim.pool.items[t].id;
    /* Both answers are bites; the target's bites hit the empty cell
     * ahead of it. */
    Action script[] = {{ACT_BITE, 1}, {ACT_BITE, 1}};
    brain_script_load(&sim.brain, script, 2);
    sim_step(&sim);
    CHECK(!sim.pool.items[t].alive || sim.pool.items[t].id != target_id);
    int cell = world_idx(&sim.world, 4, 5);
    CHECK(sim.world.occupant[cell] == -1);
    CHECK(near(sim.world.carrion[cell], set.body_energy * 0.5f));
    CHECK(near(sim.world.carrion_level[cell], 2.5f));
    sim_free(&sim);
}

static void test_digestion_costs(void)
{
    Settings s = settings_default();
    s.digest_upkeep = 0.1f;
    Creature c = {0}; /* size, speed and sight genes 0 */
    c.genome.g[GENE_PLANT] = 0.0f;
    float none = creature_upkeep_cost(&c, &s);
    c.genome.g[GENE_PLANT] = 1.0f;
    float specialist = creature_upkeep_cost(&c, &s);
    c.genome.g[GENE_MEAT] = 1.0f;
    float generalist = creature_upkeep_cost(&c, &s);
    CHECK(near(specialist - none, 0.1f));
    CHECK(near(generalist - none, 0.4f)); /* (1 + 1)^2 = 4 times */
    /* A little meat on top of full plants costs little more at first. */
    c.genome.g[GENE_MEAT] = 0.1f;
    CHECK(near(creature_upkeep_cost(&c, &s) - specialist, 0.1f * 0.21f));
}

static void test_old_age(void)
{
    Settings s = settings_default();
    s.max_age = 100;
    Creature c = {.alive = true, .energy = 1000.0f, .age = 98};
    CHECK(creature_upkeep(&c, &s));  /* turns 99 */
    CHECK(!creature_upkeep(&c, &s)); /* turns 100: dies, energy or not */
    CHECK(c.energy > 0.0f);

    /* No limit: only energy decides, and being this old costs a lot of it. */
    s.max_age = 0;
    c.age = 1000000;
    c.energy = 1e7f;
    CHECK(creature_upkeep(&c, &s));
}

static void test_upkeep_rises_with_age(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true};
    c.genome.g[GENE_AGEING] = 0.5f;
    c.age = 0;
    float young = creature_upkeep_cost(&c, &s);
    c.age = 500;
    float middle = creature_upkeep_cost(&c, &s);
    c.age = 5000;
    float old = creature_upkeep_cost(&c, &s);
    CHECK(young < middle);
    CHECK(middle < old);
}

/* Slow ageing is a trade, not a gift: dearer while young, cheaper when old. */
static void test_slow_ageing_pays_later(void)
{
    Settings s = settings_default();
    Creature slow = {.alive = true};
    Creature fast = {.alive = true};
    slow.genome.g[GENE_AGEING] = 0.0f;
    fast.genome.g[GENE_AGEING] = 1.0f;
    CHECK(creature_upkeep_cost(&slow, &s) > creature_upkeep_cost(&fast, &s));
    slow.age = 20000;
    fast.age = 20000;
    CHECK(creature_upkeep_cost(&slow, &s) < creature_upkeep_cost(&fast, &s));
}

/* No tick kills a fed creature: it outlives the old fixed limit by far, and
 * what ends it is running out of energy. */
static void test_no_fixed_death_age(void)
{
    Settings s = settings_default();
    CHECK(s.max_age == 0);
    Creature c = {.alive = true, .energy = 1e7f};
    c.genome.g[GENE_AGEING] = 0.5f;
    while (c.age < 6000) {
        CHECK(creature_upkeep(&c, &s));
    }
    CHECK(c.energy > 0.0f);
    c.energy = 0.01f;
    CHECK(!creature_upkeep(&c, &s));
}

static void test_ageing_cost_stays_finite(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true, .energy = 100.0f, .age = UINT32_MAX};
    c.genome.g[GENE_AGEING] = 1.0f;
    CHECK(isfinite(creature_upkeep_cost(&c, &s)));
    /* It stays at its top instead of wrapping back to young. */
    creature_upkeep(&c, &s);
    CHECK(c.age == UINT32_MAX);
}

static void test_hold_then_rest(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int i = spawn(&sim.pool, &sim.world, 4, 4);
    sim.pool.items[i].energy = 100.0f;
    sim.pool.items[i].heading = HEADING_EAST;
    Action script[] = {{ACT_MOVE, 3}};
    brain_script_load(&sim.brain, script, 1);

    int xs[5];
    for (int t = 0; t < 5; t++) {
        sim_step(&sim);
        xs[t] = sim.pool.items[i].x;
    }
    /* Moves on three ticks, then rests. */
    CHECK(xs[0] == 5 && xs[1] == 6 && xs[2] == 7);
    CHECK(xs[3] == 7 && xs[4] == 7);
    CHECK(sim.pool.items[i].current.kind == ACT_REST);
    /* Asked on tick 1, then not again until the hold ran out: ticks 4
     * and 5 (each "not ready" rests one tick and asks again). */
    CHECK(brain_script_decisions(&sim.brain) == 3);
    sim_free(&sim);
}

static void test_floor_restocks(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.min_creatures = 5;
    sim_step(&sim);
    CHECK(sim.pool.count >= 5);
    /* Every creature killed: the next step brings the count back up. */
    for (int i = 0; i < sim.pool.capacity; i++) {
        if (sim.pool.items[i].alive) {
            pool_kill(&sim.pool, &sim.world, i, &sim.set);
        }
    }
    CHECK(sim.pool.count == 0);
    sim_step(&sim);
    CHECK(sim.pool.count >= 5);
    CHECK(brain_script_births(&sim.brain) >= 10);
    sim_free(&sim);
}

static void test_full_pool_runs(void)
{
    /* The random brain tries to mate often; with a pool of 8 started full on
     * a small world, births must be refused without harm, and counted. The
     * checkers in the test build report any out-of-bounds access. */
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "random");
    set.pool_capacity = 8;
    set.start_share = 1.0f; /* stops at the pool's 8 */
    set.min_creatures = 8;
    set.mate_threshold = 1.0f; /* everyone can mate at once */
    set.mate_age = 0;
    Sim sim;
    CHECK(sim_init(&sim, &set, 3, 10, 10));
    CHECK(sim.pool.count == 8);
    int over = 0;
    for (int t = 0; t < 1000; t++) {
        sim_step(&sim);
        if (sim.pool.count > 8) {
            over++;
        }
    }
    CHECK(over == 0);
    CHECK(sim.pool.births_refused > 0);
    sim_free(&sim);
}

static void test_bad_brain_name(void)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "nope");
    Sim sim;
    CHECK(!sim_init(&sim, &set, 1, 16, 16));
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_pool_slots_and_ids();
    test_spawn_needs_free_soil();
    test_move_wraps();
    test_move_blocked();
    test_turns();
    test_bite();
    test_mate_needs_both();
    test_mate_costs_both_parents();
    test_mate_needs_adults();
    test_child_surname_from_a_parent();
    test_mate_no_room();
    test_pool_full_counts_refused();
    test_senses_ready_and_alike();
    test_season_drives_fed_adults();
    test_season_leaves_the_action();
    test_out_of_season_needs_a_choice();
    test_senses_season();
    test_senses_blocked();
    test_senses_plant_along_ray();
    test_empty_bite_costs();
    test_plant_eater_cap();
    test_vigorous_plants_feed_less();
    test_cap_by_level();
    test_sense();
    test_level();
    test_bite_carrion();
    test_attack_chance();
    test_attacks_hit_half_the_time();
    test_who_attacks();
    test_food_under_a_creature();
    test_slow_creature();
    test_ray_sees_creature();
    test_ray_limits();
    test_starved_creature_dies();
    test_killed_creature_leaves_carrion();
    test_digestion_costs();
    test_old_age();
    test_upkeep_rises_with_age();
    test_slow_ageing_pays_later();
    test_no_fixed_death_age();
    test_ageing_cost_stays_finite();
    test_hold_then_rest();
    test_floor_restocks();
    test_full_pool_runs();
    test_bad_brain_name();
    TEST_SUMMARY();
}
