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
 * default size_min 0.5 and birth_size_max 1.5), sight 1, a plant-eater. Tests
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
    pool_kill(&p, &w, b, &s, DEATH_WOUNDS);
    CHECK(p.count == 2);
    CHECK(w.occupant[world_idx(&w, 2, 1)] == -1);
    pool_kill(&p, &w, b, &s, DEATH_WOUNDS); /* killing a free slot changes nothing */
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
    creature_act(&w, &p, i, (Action){.kind = ACT_MOVE, .hold = 1}, &s, &r);
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
    creature_act(&w, &p, i, (Action){.kind = ACT_MOVE, .hold = 1}, &s, &r);
    CHECK(p.items[i].x == 3 && p.items[i].y == 3);
    CHECK(fabsf(p.items[i].energy - (10.0f - s.move_cost)) < 1e-6f);

    /* Turn right to face east, where there is water, then rock. */
    creature_act(&w, &p, i, (Action){.kind = ACT_TURN_RIGHT, .hold = 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_EAST);
    w.terrain[world_idx(&w, 4, 3)] = TERRAIN_WATER;
    creature_act(&w, &p, i, (Action){.kind = ACT_MOVE, .hold = 1}, &s, &r);
    w.terrain[world_idx(&w, 4, 3)] = TERRAIN_ROCK;
    creature_act(&w, &p, i, (Action){.kind = ACT_MOVE, .hold = 1}, &s, &r);
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
    creature_act(&w, &p, i, (Action){.kind = ACT_TURN_LEFT, .hold = 1}, &s, &r);
    CHECK(p.items[i].heading == HEADING_WEST); /* left of north */
    creature_act(&w, &p, i, (Action){.kind = ACT_TURN_RIGHT, .hold = 1}, &s, &r);
    creature_act(&w, &p, i, (Action){.kind = ACT_TURN_RIGHT, .hold = 1}, &s, &r);
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
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.veg[world_idx(&w, 3, 2)] == 2);
    CHECK(fabsf(p.items[i].energy - (10.0f + s.bite_veg_energy)) < 1e-6f);

    /* A plant beside the creature, not ahead, is eaten just the same. */
    w.veg[world_idx(&w, 3, 2)] = 0;
    w.veg[world_idx(&w, 2, 4)] = 1; /* behind, to the left */
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.veg[world_idx(&w, 2, 4)] == 0);

    /* With nothing to eat around, a bite gains nothing. */
    float before = p.items[i].energy;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
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
    Action mate = {.kind = ACT_MATE, .hold = 1};

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
    Action mate = {.kind = ACT_MATE, .hold = 1};
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
    Action mate = {.kind = ACT_MATE, .hold = 1};

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
    Action mate = {.kind = ACT_MATE, .hold = 1};
    for (int k = 0; k < 100; k++) {
        adult_pair(&w, &p, &a, &b, &s, &r);
        p.items[a].level = 2.0f;
        p.items[b].level = 3.0f;
        creature_start_family(&p.items[a]);
        creature_start_family(&p.items[b]);
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
    Action mate = {.kind = ACT_MATE, .hold = 1};
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
    Action mate = {.kind = ACT_MATE, .hold = 1};
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
    Action rest = {.kind = ACT_REST, .hold = 1};
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
    CHECK(creature_act(&w, &p, a, (Action){.kind = ACT_MOVE, .hold = 1}, &s, &r) == -1);
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
    Action rest = {.kind = ACT_REST, .hold = 1};
    Action mate = {.kind = ACT_MATE, .hold = 1};
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
    pool_kill(&p, &w, other, &s, DEATH_WOUNDS);
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
    pool_kill(&p, &w, other, &s, DEATH_WOUNDS);

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
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(near(p.items[i].energy, 9.75f));

    w.veg[world_idx(&w, 3, 2)] = 3;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(near(p.items[i].energy, 9.75f + s.bite_veg_energy));

    /* Off is off. */
    w.veg[world_idx(&w, 3, 2)] = 0;
    s.bite_empty_cost = 0.0f;
    float before = p.items[i].energy;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
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
    Action mate = {.kind = ACT_MATE, .hold = 1};

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
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(fabsf(p.items[i].energy - (10.0f + 0.5f * s.bite_veg_energy)) <
          1e-5f);

    p.items[i].energy = 10.0f;
    w.veg[ahead] = 1;
    w.veg_rule[ahead] = veg_rule_pack(4, 4, 0);
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(fabsf(p.items[i].energy - (10.0f + s.bite_veg_energy)) < 1e-5f);

    /* Given the choice, a bite takes the more nourishing plant. */
    int behind = world_idx(&w, 3, 4);
    w.veg[ahead] = 1;
    w.veg_rule[ahead] = veg_rule_pack(1, 4, 7);
    w.veg[behind] = 1;
    w.veg_rule[behind] = veg_rule_pack(4, 4, 0);
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
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
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(near(p.items[i].energy, 15.0f));
    CHECK(w.carrion[ahead] == 0.0f);
    CHECK(near(p.items[i].level, 3.0f)); /* 1 + level of the meal */

    carrion_add(&w, ahead, 20.0f, 2.0f);
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(near(p.items[i].energy, 23.0f)); /* at most one bite of 8 */
    CHECK(near(w.carrion[ahead], 12.0f));

    /* Both carrion and plants ahead: a plant-eater eats the plants. */
    p.items[i].genome.g[GENE_MEAT] = 0.0f;
    p.items[i].genome.g[GENE_PLANT] = 1.0f;
    w.veg[ahead] = 2;
    float carrion_before = w.carrion[ahead];
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.veg[ahead] == 1);
    CHECK(w.carrion[ahead] == carrion_before);
    /* A meat-eater, the carrion. */
    p.items[i].genome.g[GENE_MEAT] = 1.0f;
    p.items[i].genome.g[GENE_PLANT] = 0.0f;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.veg[ahead] == 1);
    CHECK(w.carrion[ahead] < carrion_before);
    w.veg[ahead] = 0;

    /* A pure plant-eater gets nothing from meat, and its level stays. */
    p.items[i].genome.g[GENE_MEAT] = 0.0f;
    p.items[i].genome.g[GENE_PLANT] = 1.0f;
    float before = p.items[i].energy;
    float level_before = p.items[i].level;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(near(p.items[i].energy, before));
    CHECK(p.items[i].level == level_before);

    /* With a floor of 0.25, the same plant-eater gets a quarter of what
     * it takes, and its level moves towards the carrion's. */
    s.digest_floor = 0.25f;
    carrion_add(&w, ahead, 20.0f, 4.0f); /* a full bite of 8 */
    before = p.items[i].energy;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
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
    p.items[a].energy = 1e6f;
    p.items[t].energy = 50.0f;
    s.family_peace = 2.0f; /* this is about the odds: identical genomes */
    s.colour_peace = 0.0f;
    /* The hit ring would otherwise change nothing, as a sole attacker is never
     * one of the others; the health is reset by hand each time. */
    int hits = 0, misses_checked = 0;
    for (int k = 0; k < 10000; k++) {
        p.items[t].health = 1000.0f;
        float attacker_before = p.items[a].energy;
        creature_act(&w, &p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, &s, &r);
        if (p.items[t].health < 1000.0f) {
            hits++;
            CHECK(near(p.items[t].health, 1000.0f - s.bite_damage * 1.0f));
        } else if (misses_checked < 10) {
            /* A miss still costs the attacker; the target is untouched. */
            CHECK(p.items[t].health == 1000.0f);
            misses_checked++;
        }
        CHECK(p.items[t].energy == 50.0f);
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

/* Sets a creature's network weights apart from an all-zero one's, so the two are
 * not close relatives and an attack between them strikes. */
static void make_stranger(Creature *c)
{
    for (int i = GENE_TRAITS; i < GENOME_LEN; i++) {
        c->genome.g[i] = 1.0f;
    }
    c->genome.g[GENE_HUE] = 0.5f; /* and far from an all-zero colour */
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
    make_stranger(&p->items[*t]);
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
    creature_act(&w, &p, a, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
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
        creature_act(&w, &p, i, (Action){.kind = ACT_MOVE, .hold = 1}, &s, &r);
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
    /* Out of energy on this tick, and with less health than a tick of it takes. */
    sim.pool.items[i].health = set.starve_damage / 2.0f;
    sim_step(&sim);
    CHECK(!sim.pool.items[i].alive);
    CHECK(sim.pool.count == 0);
    CHECK(sim.world.occupant[world_idx(&sim.world, 4, 4)] == -1);
    CHECK(brain_script_deaths(&sim.brain) == 1);
    /* The body is left as carrion, at the creature's level. No energy was
     * left, so only the body counts. */
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
    sim.set.family_peace = 2.0f; /* identical but for size */
    sim.set.colour_peace = 0.0f;
    int a = spawn(&sim.pool, &sim.world, 4, 6);
    int t = spawn(&sim.pool, &sim.world, 4, 5); /* ahead of a */
    sim.pool.items[a].genome.g[GENE_SIZE] = 1.0f; /* size 1.5 */
    sim.pool.items[t].genome.g[GENE_SIZE] = 0.0f; /* size 0.5 */
    sim.pool.items[a].energy = 100.0f;
    sim.pool.items[a].health = 100.0f;
    sim.pool.items[t].energy = 3.0f; /* its food is there for whoever eats it */
    sim.pool.items[t].health = 1.0f; /* one hit is enough */
    sim.pool.items[t].level = 2.5f;
    uint32_t target_id = sim.pool.items[t].id;
    /* Both answers are attacks; the target's swings at the empty cell ahead of it. */
    Action script[] = {{.kind = ACT_ATTACK, .hold = 1}, {.kind = ACT_ATTACK, .hold = 1}};
    brain_script_load(&sim.brain, script, 2);
    sim_step(&sim);
    CHECK(!sim.pool.items[t].alive || sim.pool.items[t].id != target_id);
    int cell = world_idx(&sim.world, 4, 5);
    CHECK(sim.world.occupant[cell] == -1);
    /* The body, and what energy it had left after its own swing and upkeep. */
    CHECK(sim.world.carrion[cell] > set.body_energy * 0.5f);
    CHECK(sim.world.carrion[cell] < set.body_energy * 0.5f + 3.0f);
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
    Creature c = {.alive = true, .energy = 1000.0f, .health = 1.0f, .age = 98};
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
    Creature c = {.alive = true, .energy = 1e7f, .health = 1.0f};
    c.genome.g[GENE_AGEING] = 0.5f;
    /* A for and not a while: a creature that dies stops ageing, and a loop
     * waiting for its age would then never end. */
    for (int k = 0; k < 6000; k++) {
        CHECK(creature_upkeep(&c, &s));
    }
    CHECK(c.age == 6000);
    CHECK(c.energy > 0.0f);
    c.energy = 0.01f;
    c.health = s.starve_damage / 2.0f;
    CHECK(!creature_upkeep(&c, &s));
}

static void test_ageing_cost_stays_finite(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true, .energy = 100.0f, .health = 1.0f,
                  .age = UINT32_MAX};
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
    Action script[] = {{.kind = ACT_MOVE, .hold = 3}};
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
            pool_kill(&sim.pool, &sim.world, i, &sim.set, DEATH_WOUNDS);
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

/* A world with two creatures side by side, the first facing the second, both
 * at full health and well fed: the scene for most fight tests. */
static void fight_scene(World *w, Pool *p, const Settings *s, int *a, int *t)
{
    two_neighbours(w, p, a, t);
    p->items[*a].health = creature_max_health(&p->items[*a], s);
    p->items[*t].health = creature_max_health(&p->items[*t], s);
}

static void test_hit_takes_health_not_energy(void)
{
    Settings s = settings_default();
    s.attack_steepness = 16.0f;
    Rng r;
    rng_seed(&r, 3);
    World w;
    Pool p;
    int a, t;
    fight_scene(&w, &p, &s, &a, &t);
    p.items[a].genome.g[GENE_SIZE] = 1.0f; /* far larger: almost always hits */
    p.items[t].genome.g[GENE_SIZE] = 0.0f;
    float health = p.items[t].health;
    for (int k = 0; k < 5 && p.items[t].health == health; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, &s, &r);
    }
    CHECK(near(p.items[t].health, health - s.bite_damage * 1.5f));
    CHECK(p.items[t].energy == 100.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_zero_energy_loses_health_slowly(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true, .energy = 0.0f, .health = 10.0f};
    CHECK(creature_upkeep(&c, &s));
    CHECK(near(c.health, 10.0f - s.starve_damage));
    CHECK(c.energy == 0.0f); /* never below 0: a debt would outlive the hunger */
}

static void test_death_only_at_zero_health(void)
{
    Settings s = settings_default();
    /* No energy, some health: alive. */
    Creature c = {.alive = true, .energy = 0.0f,
                  .health = s.starve_damage * 2.5f};
    CHECK(creature_upkeep(&c, &s));
    CHECK(creature_upkeep(&c, &s));
    CHECK(!creature_upkeep(&c, &s));
    /* Plenty of energy, no health: dead, and upkeep cannot heal it back. */
    Creature d = {.alive = true, .energy = 1000.0f, .health = -1.0f};
    CHECK(!creature_upkeep(&d, &s));
    CHECK(d.health <= 0.0f);
}

static void test_healing_costs_energy(void)
{
    Settings s = settings_default();
    Creature whole = {.alive = true, .energy = 100.0f};
    whole.health = creature_max_health(&whole, &s);
    Creature hurt = whole;
    hurt.health = whole.health - 5.0f;
    CHECK(creature_upkeep(&whole, &s));
    CHECK(creature_upkeep(&hurt, &s));
    CHECK(whole.health == creature_max_health(&whole, &s)); /* no higher */
    CHECK(near(hurt.health, creature_max_health(&hurt, &s) - 5.0f + s.heal_rate));
    CHECK(near(whole.energy - hurt.energy, s.heal_rate * s.heal_cost));
    /* The last of a wound costs only what is left of it. */
    Creature nearly = whole;
    nearly.energy = 100.0f;
    nearly.health = creature_max_health(&nearly, &s) - s.heal_rate / 2.0f;
    Creature full = nearly;
    full.health = creature_max_health(&full, &s);
    CHECK(creature_upkeep(&nearly, &s));
    CHECK(creature_upkeep(&full, &s));
    CHECK(near(nearly.health, creature_max_health(&nearly, &s)));
    CHECK(near(full.energy - nearly.energy, s.heal_rate / 2.0f * s.heal_cost));
}

static void test_no_healing_when_starving(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true, .energy = 0.0f, .health = 5.0f};
    CHECK(creature_upkeep(&c, &s));
    CHECK(c.health < 5.0f);
    /* Healing never spends more energy than there is. */
    s.upkeep_base = 0.0f;
    s.size_upkeep = s.speed_upkeep = s.sight_upkeep = s.digest_upkeep = 0.0f;
    s.slow_ageing_upkeep = 0.0f;
    s.heal_cost = 100.0f;
    Creature d = {.alive = true, .energy = 1.0f, .health = 5.0f};
    CHECK(creature_upkeep(&d, &s));
    CHECK(d.energy >= 0.0f);
    CHECK(near(d.health, 5.0f + 1.0f / 100.0f));
}

static void test_prey_can_fight_back(void)
{
    Settings s = settings_default();
    s.attack_steepness = 16.0f;
    Rng r;
    rng_seed(&r, 5);
    World w;
    Pool p;
    int a, t;
    fight_scene(&w, &p, &s, &a, &t);
    /* t is the hunter; a, a plant-eater from spawn, is the one attacking. */
    make_hunter(&p.items[t]);
    p.items[a].genome.g[GENE_SIZE] = 1.0f;
    float health = p.items[t].health;
    for (int k = 0; k < 20; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health < health);
    pool_free(&p);
    world_free(&w);
}

static void test_attack_needs_a_target_ahead(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 5);
    World w;
    Pool p;
    int a, t;
    fight_scene(&w, &p, &s, &a, &t);
    /* Beside it, not ahead: a keeps facing east, t is to the north. */
    p.items[a].heading = HEADING_EAST;
    float health = p.items[t].health;
    uint64_t draws_before = r.s[0];
    creature_act(&w, &p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, &s, &r);
    CHECK(p.items[t].health == health);
    CHECK(near(p.items[a].energy, 100.0f - s.attack_cost));
    CHECK(r.s[0] == draws_before); /* nothing to hit: no draw */
    CHECK(creature_attack_target(&w, &p, a, &s) == -1);
    p.items[a].heading = HEADING_NORTH;
    CHECK(creature_attack_target(&w, &p, a, &s) == t);
    /* One already at 0 this tick has nothing left to hit. */
    p.items[t].health = 0.0f;
    CHECK(creature_attack_target(&w, &p, a, &s) == -1);
    pool_free(&p);
    world_free(&w);
}

static void test_bite_never_attacks(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 3);
    World w;
    Pool p;
    int a, t;
    fight_scene(&w, &p, &s, &a, &t);
    make_hunter(&p.items[a]);
    float health = p.items[t].health;
    for (int k = 0; k < 20; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health == health);
    CHECK(p.items[a].energy == 100.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_odds_rise_with_others(void)
{
    float alone = attack_chance(1.0f, 1.0f, 3.0f);
    CHECK(near(attack_odds(1.0f, 1.0f, 3.0f, 0, 0.5f), alone));
    CHECK(near(attack_odds(2.0f, 1.0f, 3.0f, 0, 0.5f),
               attack_chance(2.0f, 1.0f, 3.0f)));
    float last = alone;
    for (int others = 1; others <= 50; others++) {
        float odds = attack_odds(1.0f, 1.0f, 3.0f, others, 0.5f);
        CHECK(odds > last);
        CHECK(odds < 1.0f);
        last = odds;
    }
    /* No bonus, no change. */
    CHECK(near(attack_odds(1.0f, 1.0f, 3.0f, 9, 0.0f), alone));
    /* Sizes far apart at the steepest setting stay real numbers. */
    float huge = attack_odds(1000000.0f, 0.5f, 16.0f, 3, 0.5f);
    float tiny = attack_odds(0.5f, 1000000.0f, 16.0f, 3, 0.5f);
    CHECK(huge > 0.99f && huge <= 1.0f);
    CHECK(tiny >= 0.0f && tiny < 0.01f);
}

static void test_others_counts_distinct_recent(void)
{
    Creature t = {.alive = true};
    Hit first = {.by = 7, .tick = 100};
    t.recent_hits[0] = first;
    t.hits_taken = 1;
    /* A fresh ring is not a hit by creature 0 at tick 0. */
    Creature fresh = {.alive = true};
    CHECK(creature_others_recently(&fresh, 9, 0, 10) == 0);
    /* Repeats by one attacker count once. */
    t.recent_hits[1] = (Hit){.by = 7, .tick = 101};
    t.recent_hits[2] = (Hit){.by = 8, .tick = 102};
    t.hits_taken = 3;
    CHECK(creature_others_recently(&t, 9, 102, 10) == 2);
    /* The attacker itself does not count. */
    CHECK(creature_others_recently(&t, 7, 102, 10) == 1);
    /* Only what lies within the window. */
    CHECK(creature_others_recently(&t, 9, 111, 10) == 2); /* 7 at 101 still is */
    CHECK(creature_others_recently(&t, 9, 112, 10) == 1);
    CHECK(creature_others_recently(&t, 9, 113, 10) == 0);
    /* A window of 0 still counts this very tick. */
    CHECK(creature_others_recently(&t, 9, 102, 0) == 1);
}

static void test_hits_fill_the_ring_and_wrap(void)
{
    Settings s = settings_default();
    s.attack_steepness = 16.0f;
    s.gang_window = 1000;
    Rng r;
    rng_seed(&r, 3);
    World w;
    Pool p;
    int a, t;
    fight_scene(&w, &p, &s, &a, &t);
    p.items[a].genome.g[GENE_SIZE] = 1.0f;
    p.items[t].genome.g[GENE_SIZE] = 0.0f;
    p.items[t].health = 1e6f;
    for (int k = 0; k < HITS_KEPT * 3; k++) {
        p.now = (uint32_t)k;
        creature_act(&w, &p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].hits_taken > (uint32_t)HITS_KEPT);
    for (int k = 0; k < HITS_KEPT; k++) {
        CHECK(p.items[t].recent_hits[k].by == p.items[a].id);
    }
    pool_free(&p);
    world_free(&w);
}

static void test_rule_never_asks_who_is_related(void)
{
    /* Attackers identical to the target, then strangers to it, by genome: the
     * rule sees only ids and ticks, so the same hits give the same count. */
    Settings s = settings_default();
    (void)s;
    Creature kin = {.alive = true, .hits_taken = 2};
    kin.recent_hits[0] = (Hit){.by = 1, .tick = 50};
    kin.recent_hits[1] = (Hit){.by = 2, .tick = 50};
    Creature stranger = kin;
    for (int i = 0; i < GENOME_LEN; i++) {
        stranger.genome.g[i] = 1.0f - kin.genome.g[i];
    }
    CHECK(creature_others_recently(&kin, 3, 50, 10) ==
          creature_others_recently(&stranger, 3, 50, 10));
}

static void test_death_causes_counted(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.attack_steepness = 16.0f;
    sim.set.family_peace = 2.0f; /* identical but for size */
    sim.set.colour_peace = 0.0f;
    int a = spawn(&sim.pool, &sim.world, 4, 6);
    int t = spawn(&sim.pool, &sim.world, 4, 5); /* ahead of a */
    int hungry = spawn(&sim.pool, &sim.world, 1, 1);
    Creature *pa = &sim.pool.items[a];
    pa->genome.g[GENE_SIZE] = 1.0f;
    pa->energy = 100.0f;
    pa->health = 100.0f;
    sim.pool.items[t].genome.g[GENE_SIZE] = 0.0f;
    sim.pool.items[t].energy = 100.0f;
    sim.pool.items[t].health = 0.01f;
    sim.pool.items[hungry].energy = 0.0f;
    sim.pool.items[hungry].health = sim.set.starve_damage / 2.0f;
    Action script[] = {{.kind = ACT_ATTACK, .hold = 1}, {.kind = ACT_ATTACK, .hold = 1}, {.kind = ACT_ATTACK, .hold = 1}};
    brain_script_load(&sim.brain, script, 3);
    for (int k = 0; k < 10 && sim.pool.deaths[DEATH_WOUNDS] == 0; k++) {
        sim_step(&sim);
    }
    CHECK(sim.pool.deaths[DEATH_WOUNDS] == 1);
    CHECK(sim.pool.deaths[DEATH_HUNGER] == 1);
    CHECK(sim.pool.deaths[DEATH_AGE] == 0);
    sim_free(&sim);
}

static void test_age_limit_is_its_own_cause(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.max_age = 1;
    int i = spawn(&sim.pool, &sim.world, 2, 2);
    sim.pool.items[i].energy = 100.0f;
    sim.pool.items[i].health = 100.0f;
    Action script[] = {{.kind = ACT_REST, .hold = 1}};
    brain_script_load(&sim.brain, script, 1);
    sim_step(&sim);
    CHECK(sim.pool.deaths[DEATH_AGE] == 1);
    sim_free(&sim);
}

static void test_born_at_full_health(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 3);
    World w;
    Pool p;
    int a, t;
    make_world(&w, 8, 8);
    CHECK(pool_init(&p, 3)); /* room for the child */
    a = spawn(&p, &w, 3, 3);
    t = spawn(&p, &w, 3, 2);
    p.items[a].energy = p.items[t].energy = 100.0f;
    p.items[a].age = p.items[t].age = (uint32_t)s.mate_age;
    p.items[t].mate_wish = 5;
    int child = creature_act(&w, &p, a, (Action){.kind = ACT_MATE, .hold = 1}, &s, &r);
    CHECK(child >= 0);
    CHECK(near(p.items[child].health,
               creature_max_health(&p.items[child], &s)));
    pool_free(&p);
    world_free(&w);
}

/* A hit from one creature of size 1 on another, with these two genes. */
static float damage_with(float weapon, float armour, const Settings *s)
{
    World w;
    Pool p;
    int a, t;
    Rng r;
    rng_seed(&r, 3);
    two_neighbours(&w, &p, &a, &t);
    p.items[a].genome.g[GENE_WEAPON] = weapon;
    p.items[t].genome.g[GENE_ARMOUR] = armour;
    p.items[t].health = 1000.0f;
    /* Attacks until one lands: even odds, so this ends after a few. */
    for (int k = 0; k < 200 && p.items[t].health == 1000.0f; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, s, &r);
    }
    float damage = 1000.0f - p.items[t].health;
    pool_free(&p);
    world_free(&w);
    return damage;
}

static void test_armour_reduces_damage(void)
{
    Settings s = settings_default();
    float bare = damage_with(0.0f, 0.0f, &s);
    CHECK(near(bare, s.bite_damage));
    CHECK(near(damage_with(0.0f, 1.0f, &s), bare * (1.0f - s.armour_block)));
    CHECK(damage_with(0.0f, 0.5f, &s) < bare);
    CHECK(damage_with(0.0f, 0.5f, &s) > damage_with(0.0f, 1.0f, &s));
}

static void test_weapon_raises_damage(void)
{
    Settings s = settings_default();
    float bare = damage_with(0.0f, 0.0f, &s);
    CHECK(near(damage_with(1.0f, 0.0f, &s), bare * (1.0f + s.weapon_damage)));
    CHECK(damage_with(0.5f, 0.0f, &s) > bare);
}

static void test_both_cost_upkeep(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true};
    float bare = creature_upkeep_cost(&c, &s);
    c.genome.g[GENE_ARMOUR] = 1.0f;
    CHECK(near(creature_upkeep_cost(&c, &s) - bare, s.armour_upkeep));
    c.genome.g[GENE_ARMOUR] = 0.0f;
    c.genome.g[GENE_WEAPON] = 1.0f;
    CHECK(near(creature_upkeep_cost(&c, &s) - bare, s.weapon_upkeep));
}

static void test_damage_never_below_zero(void)
{
    /* A setting above 1 would make the share negative, and a hit a heal. */
    Settings s = settings_default();
    s.armour_block = 5.0f;
    CHECK(damage_with(0.0f, 1.0f, &s) == 0.0f);
}

/* A creature fed by hand: energy well above what growth needs. */
static void test_growth_needs_surplus(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true, .health = 1.0f};
    c.genome.g[GENE_GROWTH] = 1.0f;
    float size = creature_size(&c, &s);
    /* Below the line: nothing goes into body. */
    c.energy = s.growth_surplus * s.mate_threshold * size - 5.0f;
    CHECK(creature_upkeep(&c, &s));
    CHECK(creature_size(&c, &s) == size);
    /* Well above it: some does, and it is paid for. */
    c.energy = s.growth_surplus * s.mate_threshold * size + 50.0f;
    float before = c.energy;
    CHECK(creature_upkeep(&c, &s));
    float gained = creature_size(&c, &s) - size;
    CHECK(gained > 0.0f);
    CHECK(c.energy < before - gained * s.growth_cost + 1e-3f);
}

static void test_growth_follows_gene(void)
{
    Settings s = settings_default();
    Creature keeper = {.alive = true, .energy = 1000.0f, .health = 1.0f};
    Creature grower = keeper;
    keeper.genome.g[GENE_GROWTH] = 0.0f;
    grower.genome.g[GENE_GROWTH] = 1.0f;
    float start = creature_size(&keeper, &s);
    for (int k = 0; k < 10; k++) {
        keeper.energy = grower.energy = 1000.0f;
        CHECK(creature_upkeep(&keeper, &s));
        CHECK(creature_upkeep(&grower, &s));
    }
    CHECK(creature_size(&keeper, &s) == start);
    CHECK(creature_size(&grower, &s) > start);
    /* A middle gene grows by the matching share, on the same surplus. */
    Creature half = {.alive = true, .energy = 1000.0f, .health = 1.0f};
    Creature full = half;
    half.genome.g[GENE_GROWTH] = 0.5f;
    full.genome.g[GENE_GROWTH] = 1.0f;
    CHECK(creature_upkeep(&half, &s));
    CHECK(creature_upkeep(&full, &s));
    CHECK(near((creature_size(&full, &s) - start) / 2.0f,
               creature_size(&half, &s) - start));
}

static void test_no_top_size(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true, .health = 1.0f};
    c.genome.g[GENE_GROWTH] = 1.0f;
    float young = creature_upkeep_cost(&c, &s);
    for (int k = 0; k < 20000; k++) {
        c.age = 0; /* so that what rises below is the body's cost, not age's */
        c.energy = 1e6f;
        CHECK(creature_upkeep(&c, &s));
    }
    float size = creature_size(&c, &s);
    CHECK(size > s.birth_size_max * 100.0f);
    CHECK(isfinite(size));
    c.age = 0;
    CHECK(creature_upkeep_cost(&c, &s) > young + s.size_upkeep * 99.0f);
    /* Everything built on size stays a real number. */
    CHECK(isfinite(creature_max_health(&c, &s)));
    CHECK(isfinite(attack_chance(size, 0.5f, 16.0f)));
    CHECK(isfinite(attack_chance(0.5f, size, 16.0f)));
}

static void test_big_bites_more(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 3);
    World w;
    make_world(&w, 8, 8);
    Pool p;
    CHECK(pool_init(&p, 2));
    int i = spawn(&p, &w, 3, 3);
    p.items[i].energy = 0.0f;
    int ahead = world_idx(&w, 3, 2);
    /* Size 1: one step of the plant. */
    w.veg[ahead] = VEG_MAX;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.veg[ahead] == VEG_MAX - 1);
    float small_gain = p.items[i].energy;
    /* Size 3: three steps, three times the food. */
    p.items[i].grown = 2.0f;
    p.items[i].energy = 0.0f;
    w.veg[ahead] = VEG_MAX;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.veg[ahead] == VEG_MAX - 3);
    CHECK(near(p.items[i].energy, 3.0f * small_gain));
    /* No more than the plant holds. */
    p.items[i].grown = 1000.0f;
    p.items[i].energy = 0.0f;
    w.veg[ahead] = 2;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.veg[ahead] == 0);
    CHECK(near(p.items[i].energy, 2.0f * small_gain));
    /* Meat: a bite scales with size, up to what lies there. */
    p.items[i].grown = 2.0f;
    p.items[i].energy = 0.0f;
    p.items[i].genome.g[GENE_MEAT] = 1.0f;
    carrion_add(&w, ahead, 100.0f, 2.0f);
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(near(w.carrion[ahead], 100.0f - s.bite_meat_energy * 3.0f));
    p.items[i].grown = 1000.0f;
    creature_act(&w, &p, i, (Action){.kind = ACT_BITE, .hold = 1}, &s, &r);
    CHECK(w.carrion[ahead] == 0.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_bite_steps_any_size(void)
{
    /* Converting a float too large for int is undefined, so this must stop
     * before the conversion. */
    CHECK(bite_steps(0.5f) == 1);
    CHECK(bite_steps(1.0f) == 1);
    CHECK(bite_steps(3.0f) == 3);
    CHECK(bite_steps(1e30f) == VEG_MAX);
}

static void test_senses_handle_any_size(void)
{
    Settings s = settings_default();
    World w;
    Pool p;
    int a, t;
    two_neighbours(&w, &p, &a, &t);
    Senses in;
    float sizes[] = {0.0f, 1.0f, 1000.0f, 1e9f};
    /* sizeof the whole array over sizeof one element: only works here, where
     * the array has not decayed to a pointer. */
    for (size_t k = 0; k < sizeof(sizes) / sizeof(sizes[0]); k++) {
        p.items[a].grown = sizes[k];
        p.items[t].grown = 1e9f - sizes[k];
        creature_sense(&w, &p, a, &s, &in);
        float own = in.v[SENSE_SIZE];
        float rel = in.v[SENSE_RAYS + RAY_AHEAD * RAY_VALUES + RAY_RELSIZE];
        CHECK(own >= 0.0f && own <= 1.0f);
        CHECK(rel >= 0.0f && rel <= 1.0f);
    }
    pool_free(&p);
    world_free(&w);
}

static void test_memory_persists(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int i = spawn(&sim.pool, &sim.world, 4, 4);
    sim.pool.items[i].energy = 100.0f;
    Action script[] = {
        {.kind = ACT_REST, .hold = 1, .memory = {0.5f, -0.25f, 1.0f}},
        {.kind = ACT_REST, .hold = 1, .memory = {0.0f, 0.0f, 0.0f}},
    };
    brain_script_load(&sim.brain, script, 2);
    sim_step(&sim);
    Senses in;
    creature_sense(&sim.world, &sim.pool, i, &sim.set, &in);
    CHECK(in.v[SENSE_MEMORY + 0] == 0.5f);
    CHECK(in.v[SENSE_MEMORY + 1] == -0.25f);
    CHECK(in.v[SENSE_MEMORY + 2] == 1.0f);
    /* Next tick the brain is shown it. */
    sim_step(&sim);
    CHECK(brain_script_last_senses(&sim.brain).v[SENSE_MEMORY] == 0.5f);
    sim_free(&sim);
}

static void test_memory_from_a_bad_brain_is_held(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int i = spawn(&sim.pool, &sim.world, 4, 4);
    sim.pool.items[i].energy = 100.0f;
    Action script[] = {
        {.kind = ACT_REST, .hold = 1, .memory = {NAN, 5.0f, -5.0f}},
    };
    brain_script_load(&sim.brain, script, 1);
    sim_step(&sim);
    CHECK(sim.pool.items[i].memory[0] == -1.0f);
    CHECK(sim.pool.items[i].memory[1] == 1.0f);
    CHECK(sim.pool.items[i].memory[2] == -1.0f);
    sim_free(&sim);
}

static void test_child_memory_starts_empty(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 3);
    World w;
    make_world(&w, 8, 8);
    Pool p;
    CHECK(pool_init(&p, 3));
    int a = spawn(&p, &w, 3, 3);
    int t = spawn(&p, &w, 3, 2);
    p.items[a].energy = p.items[t].energy = 100.0f;
    p.items[a].age = p.items[t].age = (uint32_t)s.mate_age;
    p.items[a].memory[0] = 0.75f;
    p.items[t].memory[0] = 0.75f;
    p.items[t].mate_wish = 5;
    int child = creature_act(&w, &p, a, (Action){.kind = ACT_MATE, .hold = 1},
                             &s, &r);
    CHECK(child >= 0);
    for (int m = 0; m < MEMORY_VALUES; m++) {
        CHECK(p.items[child].memory[m] == 0.0f);
    }
    pool_free(&p);
    world_free(&w);
}

static void test_senses_hunger_pain_near(void)
{
    Settings s = settings_default();
    World w;
    make_world(&w, 8, 8);
    Pool p;
    CHECK(pool_init(&p, 5));
    int c = spawn(&p, &w, 3, 3);
    p.items[c].energy = s.mate_threshold / 4.0f; /* a quarter of mating's need */
    p.items[c].health = creature_max_health(&p.items[c], &s) * 0.75f;
    Senses in;
    creature_sense(&w, &p, c, &s, &in);
    CHECK(near(in.v[SENSE_HUNGER], 0.75f));
    CHECK(near(in.v[SENSE_PAIN], 0.25f));
    CHECK(in.v[SENSE_KIN_NEAR] == 0.0f && in.v[SENSE_STRANGERS_NEAR] == 0.0f);
    CHECK(in.v[SENSE_RANDOM] == 0.0f); /* creature_sense never draws */

    /* A twin one way and two strangers the other, one of them diagonal. */
    int twin = spawn(&p, &w, 3, 2);
    p.items[twin].genome = p.items[c].genome;
    int far1 = spawn(&p, &w, 4, 4);
    int far2 = spawn(&p, &w, 2, 3);
    for (int i = 0; i < GENOME_LEN; i++) {
        p.items[far1].genome.g[i] = p.items[c].genome.g[i] + 2.0f;
        p.items[far2].genome.g[i] = p.items[c].genome.g[i] - 2.0f;
    }
    /* Two cells away is not near. */
    spawn(&p, &w, 5, 3);
    creature_sense(&w, &p, c, &s, &in);
    CHECK(in.v[SENSE_KIN_NEAR] == 1.0f / 8.0f);
    CHECK(in.v[SENSE_STRANGERS_NEAR] == 2.0f / 8.0f);

    /* Every one of them stays within 0..1 at the extremes. */
    p.items[c].energy = 0.0f;
    p.items[c].health = -5.0f;
    creature_sense(&w, &p, c, &s, &in);
    CHECK(in.v[SENSE_HUNGER] == 1.0f);
    CHECK(in.v[SENSE_PAIN] == 1.0f);
    p.items[c].energy = 1e6f;
    p.items[c].health = 1e6f; /* above an unhurt body's: no negative pain */
    creature_sense(&w, &p, c, &s, &in);
    CHECK(in.v[SENSE_HUNGER] == 0.0f);
    CHECK(in.v[SENSE_PAIN] == 0.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_random_sense_is_seeded(void)
{
    /* Two runs of one seed see the same draws, and the draws change. */
    float seen[2][8];
    for (int run = 0; run < 2; run++) {
        Sim sim;
        Settings set;
        make_sim(&sim, &set);
        int i = spawn(&sim.pool, &sim.world, 4, 4);
        sim.pool.items[i].energy = 100.0f;
        Action script[8];
        for (int k = 0; k < 8; k++) {
            script[k] = (Action){.kind = ACT_REST, .hold = 1};
        }
        brain_script_load(&sim.brain, script, 8);
        for (int k = 0; k < 8; k++) {
            sim_step(&sim);
            seen[run][k] = brain_script_last_senses(&sim.brain).v[SENSE_RANDOM];
        }
        sim_free(&sim);
    }
    bool varies = false;
    for (int k = 0; k < 8; k++) {
        CHECK(seen[0][k] == seen[1][k]);
        CHECK(seen[0][k] >= 0.0f && seen[0][k] < 1.0f);
        varies = varies || seen[0][k] != seen[0][0];
    }
    CHECK(varies);
}

static void test_attack_spares_close_relatives(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 5);
    World w;
    Pool p;
    int a, t;
    fight_scene(&w, &p, &s, &a, &t);
    CHECK(creature_attack_target(&w, &p, a, &s) == t); /* strangers */
    p.items[t].genome = p.items[a].genome; /* now twins */
    CHECK(creature_attack_target(&w, &p, a, &s) == -1);
    float health = p.items[t].health;
    for (int k = 0; k < 50; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health == health);
    CHECK(near(p.items[a].energy, 100.0f - 50.0f * s.attack_cost)); /* still paid */
    /* With both settings at their ends, nobody is spared. */
    s.family_peace = 2.0f;
    s.colour_peace = 0.0f;
    CHECK(creature_attack_target(&w, &p, a, &s) == t);
    pool_free(&p);
    world_free(&w);
}

/* Two parents of one colour and family, and a child made from them. */
static void family_birth(float hue, float mutation, uint64_t seed, Creature *child_out)
{
    Settings s = settings_default();
    s.mutation_rate = mutation;
    s.mutation_spread = 1.0f; /* any mutation is a large one */
    Rng r;
    rng_seed(&r, seed);
    World w;
    make_world(&w, 8, 8);
    Pool p;
    CHECK(pool_init(&p, 3));
    int a = spawn(&p, &w, 3, 3);
    int t = spawn(&p, &w, 3, 2);
    p.items[a].energy = p.items[t].energy = 100.0f;
    p.items[a].age = p.items[t].age = (uint32_t)s.mate_age;
    p.items[a].genome.g[GENE_HUE] = p.items[t].genome.g[GENE_HUE] = hue;
    p.items[a].family_hue = p.items[t].family_hue = hue;
    p.items[a].lineage = p.items[t].lineage = 77;
    p.items[t].mate_wish = 5;
    int child = creature_act(&w, &p, a, (Action){.kind = ACT_MATE, .hold = 1}, &s, &r);
    CHECK(child >= 0);
    *child_out = p.items[child];
    pool_free(&p);
    world_free(&w);
}

static void test_child_stays_in_family_without_drift(void)
{
    Creature c;
    family_birth(0.5f, 0.0f, 11, &c);
    CHECK(c.lineage == 77);
    CHECK(c.family_hue == 0.5f);
}

static void test_child_of_two_colours_joins_the_closer_family(void)
{
    /* No mutation, parents of far-apart colours in two families: the child's colour
     * is one parent's, so it must carry that parent's family and not found a new one. */
    Settings s = settings_default();
    s.mutation_rate = 0.0f;
    Rng r;
    rng_seed(&r, 11);
    for (int k = 0; k < 40; k++) {
        World w;
        make_world(&w, 8, 8);
        Pool p;
        CHECK(pool_init(&p, 3));
        int a = spawn(&p, &w, 3, 3);
        int t = spawn(&p, &w, 3, 2);
        p.items[a].energy = p.items[t].energy = 100.0f;
        p.items[a].age = p.items[t].age = (uint32_t)s.mate_age;
        p.items[a].genome.g[GENE_HUE] = p.items[a].family_hue = 0.1f;
        p.items[t].genome.g[GENE_HUE] = p.items[t].family_hue = 0.6f;
        p.items[a].lineage = 10;
        p.items[t].lineage = 20;
        p.items[t].mate_wish = 5;
        int child = creature_act(&w, &p, a, (Action){.kind = ACT_MATE, .hold = 1}, &s, &r);
        CHECK(child >= 0);
        uint32_t expected = p.items[child].genome.g[GENE_HUE] == 0.1f ? 10u : 20u;
        CHECK(p.items[child].lineage == expected);
        pool_free(&p);
        world_free(&w);
    }
}

static void test_drifted_child_starts_a_family(void)
{
    /* Every value mutates by a large step, so over these seeds the colour lands
     * both near and far: each child is checked against the side it landed on. */
    Settings s = settings_default();
    int left = 0, stayed = 0;
    for (uint64_t seed = 1; seed <= 60; seed++) {
        Creature c;
        family_birth(0.5f, 1.0f, seed, &c);
        float d = fabsf(c.genome.g[GENE_HUE] - 0.5f);
        d = d > 0.5f ? 1.0f - d : d;
        if (d > s.family_split) {
            left++;
            CHECK(c.lineage == c.id);
            CHECK(c.family_hue == c.genome.g[GENE_HUE]);
        } else {
            stayed++;
            CHECK(c.lineage == 77);
            CHECK(c.family_hue == 0.5f);
        }
    }
    CHECK(left > 0 && stayed > 0);
}

static void test_rest_costs_less(void)
{
    Settings s = settings_default();
    Creature resting = {.alive = true, .energy = 100.0f, .health = 1.0f};
    resting.health = creature_max_health(&resting, &s);
    Creature busy = resting;
    resting.current.kind = ACT_REST;
    busy.current.kind = ACT_TURN_LEFT;
    float cost = creature_upkeep_cost(&busy, &s);
    CHECK(creature_upkeep(&resting, &s));
    CHECK(creature_upkeep(&busy, &s));
    CHECK(near(100.0f - busy.energy, cost));
    CHECK(near(100.0f - resting.energy, cost * s.rest_upkeep));
}

static void test_crowd_counts_anyone_near(void)
{
    World w;
    make_world(&w, 16, 16);
    Pool p;
    CHECK(pool_init(&p, 6));
    int c = spawn(&p, &w, 0, 0);
    CHECK(creature_crowd(&w, &p.items[c], 1) == 0); /* it does not count itself */
    spawn(&p, &w, 1, 0);
    spawn(&p, &w, 15, 15); /* diagonal, across the wrap */
    spawn(&p, &w, 2, 0);   /* two away */
    spawn(&p, &w, 3, 0);   /* three away */
    CHECK(creature_crowd(&w, &p.items[c], 1) == 2);
    CHECK(creature_crowd(&w, &p.items[c], 2) == 3);
    CHECK(creature_crowd(&w, &p.items[c], 3) == 4);
    pool_free(&p);
    world_free(&w);
}

static void test_crowd_cost_past_the_free_ones(void)
{
    Settings s = settings_default();
    s.crowd_upkeep = 0.1f;
    s.crowd_radius = 1;
    s.crowd_free = 1;
    World w;
    make_world(&w, 16, 16);
    Pool p;
    CHECK(pool_init(&p, 4));
    int c = spawn(&p, &w, 5, 5);
    CHECK(creature_crowd_cost(&w, &p.items[c], &s) == 0.0f);
    spawn(&p, &w, 6, 5);
    CHECK(creature_crowd_cost(&w, &p.items[c], &s) == 0.0f); /* the free one */
    spawn(&p, &w, 4, 5);
    CHECK(near(creature_crowd_cost(&w, &p.items[c], &s), 0.1f));
    int stranger = spawn(&p, &w, 5, 4);
    CHECK(near(creature_crowd_cost(&w, &p.items[c], &s), 0.2f));
    /* Relatives or strangers cost the same: the rule never asks. */
    p.items[stranger].genome = p.items[c].genome;
    CHECK(near(creature_crowd_cost(&w, &p.items[c], &s), 0.2f));
    s.crowd_upkeep = 0.0f;
    CHECK(creature_crowd_cost(&w, &p.items[c], &s) == 0.0f);
    pool_free(&p);
    world_free(&w);
}

static void test_crowd_charged_each_tick(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.crowd_upkeep = 0.5f;
    sim.set.crowd_radius = 1;
    sim.set.crowd_free = 0;
    int a = spawn(&sim.pool, &sim.world, 2, 2);
    int b = spawn(&sim.pool, &sim.world, 3, 2);
    int alone = spawn(&sim.pool, &sim.world, 8, 8);
    sim.pool.items[a].energy = 50.0f;
    sim.pool.items[b].energy = 50.0f;
    sim.pool.items[alone].energy = 50.0f;
    sim.pool.items[a].genome = sim.pool.items[alone].genome;
    sim.pool.items[b].genome = sim.pool.items[alone].genome;
    sim_step(&sim);
    CHECK(near(sim.pool.items[alone].energy - sim.pool.items[a].energy, 0.5f));
    CHECK(near(sim.pool.items[alone].energy - sim.pool.items[b].energy, 0.5f));
    sim_free(&sim);
}

/* An adult with a full territory gene and a certain drive, a stranger to the east,
 * facing north, its brain having said to rest. */
static void territory_scene(World *w, Pool *p, Settings *s, int *a, int *t)
{
    fight_scene(w, p, s, a, t); /* t starts ahead: north */
    p->items[*a].age = (uint32_t)s->mate_age;
    p->items[*a].genome.g[GENE_TERRITORY] = 1.0f;
    s->territory_drive = 1.0f;
    s->attack_steepness = 0.0f; /* even odds whatever the sizes */
}

static void test_territory_attacks_a_stranger_beside_it(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 5);
    World w;
    Pool p;
    int a, t;
    territory_scene(&w, &p, &s, &a, &t);
    /* Moved to the east: a must turn to reach it. */
    w.occupant[world_idx(&w, 3, 2)] = -1;
    p.items[t].x = 4;
    p.items[t].y = 3;
    w.occupant[world_idx(&w, 4, 3)] = t;
    float health = p.items[t].health;
    for (int k = 0; k < 20; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_REST, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health < health);
    CHECK(p.items[a].heading == HEADING_EAST);
    pool_free(&p);
    world_free(&w);
}

static void test_territory_spares_relatives(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 5);
    World w;
    Pool p;
    int a, t;
    territory_scene(&w, &p, &s, &a, &t);
    p.items[t].genome = p.items[a].genome;
    float energy = p.items[a].energy;
    for (int k = 0; k < 20; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_REST, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health == creature_max_health(&p.items[t], &s));
    CHECK(p.items[a].energy == energy); /* no swing, so nothing paid */
    CHECK(p.items[a].heading == HEADING_NORTH); /* it did not turn either */
    pool_free(&p);
    world_free(&w);
}

static void test_territory_follows_the_gene_and_age(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 5);
    World w;
    Pool p;
    int a, t;
    territory_scene(&w, &p, &s, &a, &t);
    float health = p.items[t].health;
    /* Gene 0: it never fires. */
    p.items[a].genome.g[GENE_TERRITORY] = 0.0f;
    for (int k = 0; k < 50; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_REST, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health == health);
    /* A full gene on the young: not yet. */
    p.items[a].genome.g[GENE_TERRITORY] = 1.0f;
    p.items[a].age = 0;
    for (int k = 0; k < 50; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_REST, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health == health);
    pool_free(&p);
    world_free(&w);
}

static void test_territory_gene_costs_upkeep(void)
{
    Settings s = settings_default();
    Creature c = {.alive = true};
    float bare = creature_upkeep_cost(&c, &s);
    c.genome.g[GENE_TERRITORY] = 1.0f;
    CHECK(near(creature_upkeep_cost(&c, &s) - bare, s.territory_upkeep));
}

static void test_territory_does_not_stop_a_chosen_mating(void)
{
    Settings s = settings_default();
    Rng r;
    rng_seed(&r, 5);
    World w;
    Pool p;
    make_world(&w, 8, 8);
    CHECK(pool_init(&p, 3));
    int a = spawn(&p, &w, 3, 3);
    int t = spawn(&p, &w, 3, 2);
    make_stranger(&p.items[t]);
    p.items[a].energy = p.items[t].energy = 100.0f;
    p.items[a].age = p.items[t].age = (uint32_t)s.mate_age;
    p.items[a].genome.g[GENE_TERRITORY] = 1.0f;
    p.items[t].mate_wish = 5;
    s.territory_drive = 1.0f;
    int child = creature_act(&w, &p, a, (Action){.kind = ACT_MATE, .hold = 1}, &s, &r);
    CHECK(child >= 0);
    pool_free(&p);
    world_free(&w);
}

static void test_attack_spares_similar_colours(void)
{
    Settings s = settings_default();
    World w;
    Pool p;
    int a, t;
    fight_scene(&w, &p, &s, &a, &t); /* strangers by genes: only colour differs below */
    p.items[a].genome.g[GENE_HUE] = 0.5f;
    p.items[t].genome.g[GENE_HUE] = 0.5f + s.colour_peace / 2.0f;
    CHECK(creature_attack_target(&w, &p, a, &s) == -1);
    p.items[t].genome.g[GENE_HUE] = 0.5f + s.colour_peace * 2.0f;
    CHECK(creature_attack_target(&w, &p, a, &s) == t);
    /* Round the wheel: 0.98 and 0.01 look alike. */
    p.items[a].genome.g[GENE_HUE] = 0.98f;
    p.items[t].genome.g[GENE_HUE] = 0.01f;
    CHECK(creature_attack_target(&w, &p, a, &s) == -1);
    /* The instinct meets the same test. */
    s.territory_drive = 1.0f;
    p.items[a].age = (uint32_t)s.mate_age;
    p.items[a].genome.g[GENE_TERRITORY] = 1.0f;
    Rng r;
    rng_seed(&r, 5);
    float health = p.items[t].health;
    for (int k = 0; k < 30; k++) {
        creature_act(&w, &p, a, (Action){.kind = ACT_REST, .hold = 1}, &s, &r);
    }
    CHECK(p.items[t].health == health);
    pool_free(&p);
    world_free(&w);
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
    test_hit_takes_health_not_energy();
    test_zero_energy_loses_health_slowly();
    test_death_only_at_zero_health();
    test_healing_costs_energy();
    test_no_healing_when_starving();
    test_prey_can_fight_back();
    test_attack_needs_a_target_ahead();
    test_bite_never_attacks();
    test_odds_rise_with_others();
    test_others_counts_distinct_recent();
    test_hits_fill_the_ring_and_wrap();
    test_rule_never_asks_who_is_related();
    test_death_causes_counted();
    test_age_limit_is_its_own_cause();
    test_born_at_full_health();
    test_armour_reduces_damage();
    test_weapon_raises_damage();
    test_both_cost_upkeep();
    test_damage_never_below_zero();
    test_growth_needs_surplus();
    test_growth_follows_gene();
    test_no_top_size();
    test_big_bites_more();
    test_bite_steps_any_size();
    test_senses_handle_any_size();
    test_memory_persists();
    test_memory_from_a_bad_brain_is_held();
    test_child_memory_starts_empty();
    test_senses_hunger_pain_near();
    test_random_sense_is_seeded();
    test_attack_spares_close_relatives();
    test_child_stays_in_family_without_drift();
    test_child_of_two_colours_joins_the_closer_family();
    test_drifted_child_starts_a_family();
    test_rest_costs_less();
    test_crowd_counts_anyone_near();
    test_crowd_cost_past_the_free_ones();
    test_crowd_charged_each_tick();
    test_territory_attacks_a_stranger_beside_it();
    test_territory_spares_relatives();
    test_territory_follows_the_gene_and_age();
    test_territory_gene_costs_upkeep();
    test_territory_does_not_stop_a_chosen_mating();
    test_attack_spares_similar_colours();
    TEST_SUMMARY();
}
