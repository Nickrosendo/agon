#include "test.h"

#include <math.h>   /* fabsf */
#include <stdio.h>  /* snprintf */
#include <string.h> /* strstr, strlen */

#include "brain_script.h"
#include "chronicle.h"
#include "climate.h"
#include "creature.h"
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

/* pool_spawn with a size-1 plant-eating body that sees nothing. */
static int spawn(Pool *p, World *w, int x, int y)
{
    int idx = pool_spawn(p, w, x, y);
    if (idx >= 0) {
        Genome *g = &p->items[idx].genome;
        g->g[GENE_SIZE] = 0.5f;
        g->g[GENE_SPEED] = 1.0f;
        g->g[GENE_LEAF] = 1.0f;
    }
    return idx;
}

/* A 16x16 world of bare soil with the scripted brain and no founders. */
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

/* The newest event of a creature, or one with kind LIFE_COUNT when it has none. */
static LifeEvent newest(const Creature *c)
{
    if (c->events_noted == 0) {
        return (LifeEvent){.kind = LIFE_COUNT};
    }
    return c->events[(c->events_noted - 1) % CREATURE_EVENTS];
}

/* How many events of this kind are still in a creature's ring. */
static int count_kind(const Creature *c, int kind)
{
    int kept = c->events_noted < CREATURE_EVENTS ? (int)c->events_noted
                                                 : CREATURE_EVENTS;
    int n = 0;
    for (int k = 0; k < kept; k++) {
        n += c->events[k].kind == kind;
    }
    return n;
}

static void test_founders_are_born_founders(void)
{
    Settings set = settings_default();
    Sim sim;
    CHECK(sim_init(&sim, &set, 3, 32, 16));
    int seen = 0;
    for (int i = 0; i < sim.pool.capacity; i++) {
        const Creature *c = &sim.pool.items[i];
        if (c->alive) {
            seen++;
            CHECK(c->events_noted == 1);
            CHECK(c->events[0].kind == LIFE_BORN && c->events[0].value == 0);
            CHECK(c->tally.flags & TALLY_FOUNDER);
        }
    }
    CHECK(seen > 0);
    sim_free(&sim);
}

static void test_birth_names_parents_and_children(void)
{
    World w;
    make_world(&w, 8, 8);
    Pool p;
    CHECK(pool_init(&p, 8));
    Chronicle ch;
    CHECK(chronicle_init(&ch));
    p.chronicle = &ch;
    p.now = 40;
    Settings s = settings_default();
    s.birth_per_plant = 0.0f; /* a pool built by hand counts no plants */
    Rng r;
    rng_seed(&r, 5);
    int a = spawn(&p, &w, 3, 3);
    int b = spawn(&p, &w, 4, 3);
    for (int k = 0; k < 2; k++) {
        Creature *c = &p.items[k == 0 ? a : b];
        genome_random(&c->genome, &r, &s);
        c->genome.g[GENE_SIZE] = 0.5f;
        c->age = (uint32_t)s.mate_age;
        c->energy = s.mate_threshold + 10.0f;
        c->health = creature_max_health(c, &s);
    }
    /* Copy one genome to the other, so the breeding barrier cannot refuse. */
    p.items[b].genome = p.items[a].genome;
    Action mate = {.kind = ACT_MATE, .hold = 1};
    creature_act(&w, &p, b, mate, &s, &r); /* b now wishes too */
    int child = creature_act(&w, &p, a, mate, &s, &r);
    CHECK(child >= 0);
    if (child >= 0) {
        const Creature *k = &p.items[child];
        LifeEvent e = newest(k);
        CHECK(e.kind == LIFE_BORN && e.value == 1 && e.tick == 40);
        CHECK(k->tally.parents[0] == p.items[a].id);
        CHECK(k->tally.parents[1] == p.items[b].id);
        CHECK(!(k->tally.flags & TALLY_FOUNDER));
        CHECK(newest(&p.items[a]).kind == LIFE_CHILD);
        CHECK(newest(&p.items[a]).other_id == k->id);
        CHECK(newest(&p.items[b]).kind == LIFE_CHILD);
        CHECK(p.items[a].tally.children == 1 && p.items[b].tally.children == 1);
    }
    chronicle_free(&ch);
    pool_free(&p);
    world_free(&w);
}

/* Attacker a faces target t (north of it), both fed and unhurt, and the
 * attacker far larger, so that nearly every swing lands. */
static void fight_pool(World *w, Pool *p, Chronicle *ch, Settings *s, int *a,
                       int *t)
{
    make_world(w, 8, 8);
    CHECK(pool_init(p, 4));
    CHECK(chronicle_init(ch));
    p->chronicle = ch;
    *s = settings_default();
    s->attack_steepness = 16.0f;
    s->family_peace = 2.0f; /* nobody is spared as a relative */
    s->colour_peace = 0.0f; /* nor for a similar colour */
    *a = spawn(p, w, 3, 4);
    *t = spawn(p, w, 3, 3);
    p->items[*a].genome.g[GENE_SIZE] = 1.0f; /* size 1.5 */
    p->items[*t].genome.g[GENE_SIZE] = 0.0f; /* size 0.5 */
    for (int k = 0; k < 2; k++) {
        Creature *c = &p->items[k == 0 ? *a : *t];
        c->energy = 100.0f;
        c->health = creature_max_health(c, s);
    }
}

/* Swings until one lands, at most 20 times. */
static void hit_once(World *w, Pool *p, int a, int t, const Settings *s, Rng *r)
{
    uint32_t before = p->items[t].hits_taken;
    for (int k = 0; k < 20 && p->items[t].hits_taken == before; k++) {
        creature_act(w, p, a, (Action){.kind = ACT_ATTACK, .hold = 1}, s, r);
    }
    CHECK(p->items[t].hits_taken == before + 1);
}

static void test_first_hit_of_a_fight_is_noted_once(void)
{
    World w;
    Pool p;
    Chronicle ch;
    Settings s;
    int a, t;
    fight_pool(&w, &p, &ch, &s, &a, &t);
    p.items[t].health = 1000.0f; /* lives through every hit */
    Rng r;
    rng_seed(&r, 2);
    p.now = 10;
    hit_once(&w, &p, a, t, &s, &r);
    CHECK(newest(&p.items[t]).kind == LIFE_ATTACKED);
    CHECK(newest(&p.items[t]).other_id == p.items[a].id);
    p.now = 10 + FIGHT_GAP; /* still the same fight */
    hit_once(&w, &p, a, t, &s, &r);
    CHECK(count_kind(&p.items[t], LIFE_ATTACKED) == 1);
    p.now = 10 + 2 * FIGHT_GAP + 1; /* a quiet longer than the gap: a new one */
    hit_once(&w, &p, a, t, &s, &r);
    CHECK(count_kind(&p.items[t], LIFE_ATTACKED) == 2);
    chronicle_free(&ch);
    pool_free(&p);
    world_free(&w);
}

static void test_heavy_wound_noted_when_crossed(void)
{
    World w;
    Pool p;
    Chronicle ch;
    Settings s;
    int a, t;
    fight_pool(&w, &p, &ch, &s, &a, &t);
    s.health_per_size = 100.0f; /* so one hit is a small share of a body */
    Creature *target = &p.items[t];
    float full = creature_max_health(target, &s);
    /* Just above the line, and a hit is bite_damage * 1.5 (no weapon, no
     * armour), far smaller than what is left. */
    float hit = s.bite_damage * 1.5f;
    target->health = HEAVY_WOUND * full + hit * 0.5f;
    Rng r;
    rng_seed(&r, 4);
    hit_once(&w, &p, a, t, &s, &r);
    CHECK(target->health > 0.0f);
    CHECK(newest(target).kind == LIFE_WOUNDED);
    CHECK(newest(target).value >= 0 && newest(target).value < 25);
    CHECK(target->tally.heavy_wounds == 1);
    /* Already below: another hit is not another heavy wound. */
    if (target->health > hit) {
        hit_once(&w, &p, a, t, &s, &r);
        CHECK(target->tally.heavy_wounds == 1);
    }
    chronicle_free(&ch);
    pool_free(&p);
    world_free(&w);
}

static void test_kills_counted_and_hunters_named(void)
{
    World w;
    Pool p;
    Chronicle ch;
    Settings s;
    int a, t;
    fight_pool(&w, &p, &ch, &s, &a, &t);
    Rng r;
    rng_seed(&r, 6);
    p.items[t].health = 0.01f;
    hit_once(&w, &p, a, t, &s, &r);
    CHECK(newest(&p.items[a]).kind == LIFE_KILL);
    CHECK(newest(&p.items[a]).value == 1); /* its first */
    CHECK(newest(&p.items[a]).other_id == p.items[t].id);
    /* Bring a hunter back to the same place and kill it too. The hand-made pool
     * has no accounting, so the dying target is cleared by hand. */
    pool_kill(&p, &w, t, &s, DEATH_WOUNDS);
    int h = spawn(&p, &w, 3, 3);
    p.items[h].genome.g[GENE_SIZE] = 0.0f;
    p.items[h].genome.g[GENE_MEAT] = 1.0f;
    p.items[h].genome.g[GENE_LEAF] = 0.0f;
    p.items[h].health = 0.01f;
    hit_once(&w, &p, a, h, &s, &r);
    CHECK(newest(&p.items[a]).kind == LIFE_KILL_HUNTER);
    CHECK(newest(&p.items[a]).value == 2);
    CHECK(p.items[a].tally.kills == 2 && p.items[a].tally.hunter_kills == 1);
    chronicle_free(&ch);
    pool_free(&p);
    world_free(&w);
}

static void test_death_by_wounds_names_the_killer(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.attack_steepness = 50.0f;
    sim.set.family_peace = 2.0f;
    sim.set.colour_peace = 0.0f;
    int a = spawn(&sim.pool, &sim.world, 4, 6);
    int t = spawn(&sim.pool, &sim.world, 4, 5);
    sim.pool.items[a].genome.g[GENE_SIZE] = 1.0f;
    sim.pool.items[t].genome.g[GENE_SIZE] = 0.0f;
    sim.pool.items[a].energy = 100.0f;
    sim.pool.items[a].health = 100.0f;
    sim.pool.items[t].energy = 3.0f;
    sim.pool.items[t].health = 0.01f;
    uint32_t killer = sim.pool.items[a].id, victim = sim.pool.items[t].id;
    Action script[] = {{.kind = ACT_ATTACK, .hold = 1}, {.kind = ACT_ATTACK, .hold = 1}};
    brain_script_load(&sim.brain, script, 2);
    sim_step(&sim);
    const NotableLife *n = chronicle_find(&sim.chronicle, victim);
    CHECK(n != NULL);
    if (n != NULL) {
        LifeEvent e = n->events[(n->events_noted - 1) % NOTABLE_EVENTS];
        CHECK(!n->alive);
        CHECK(e.kind == LIFE_DIED && e.value == DEATH_WOUNDS);
        CHECK(e.other_id == killer);
    }
    sim_free(&sim);
}

static void test_survived_fight_after_quiet(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int c = spawn(&sim.pool, &sim.world, 8, 8);
    Creature *me = &sim.pool.items[c];
    me->energy = 1000.0f;
    me->health = creature_max_health(me, &sim.set);
    me->hits_taken = 1;
    me->recent_hits[0] = (Hit){.by = 77, .tick = 0};
    for (int k = 0; k < FIGHT_GAP; k++) {
        sim_step(&sim); /* ticks 0 to FIGHT_GAP - 1 */
    }
    CHECK(count_kind(me, LIFE_SURVIVED) == 0);
    sim_step(&sim); /* tick FIGHT_GAP */
    CHECK(newest(me).kind == LIFE_SURVIVED && newest(me).other_id == 77);
    CHECK(me->tally.fights == 1);
    sim_step(&sim);
    CHECK(me->tally.fights == 1); /* once */
    sim_free(&sim);
}

static void test_kin_lost_nearby(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int a = spawn(&sim.pool, &sim.world, 5, 5);
    int b = spawn(&sim.pool, &sim.world, 7, 6); /* 2 cells away */
    int far = spawn(&sim.pool, &sim.world, 12, 12);
    Rng r;
    rng_seed(&r, 8);
    genome_random(&sim.pool.items[a].genome, &r, &sim.set);
    sim.pool.items[b].genome = sim.pool.items[a].genome; /* kinship 1 */
    sim.pool.items[far].genome = sim.pool.items[a].genome;
    sim.pool.items[a].energy = 0.0f;
    sim.pool.items[a].health = 0.001f; /* hunger ends it this tick */
    sim.pool.items[b].energy = 1000.0f;
    sim.pool.items[b].health = 1000.0f;
    sim.pool.items[far].energy = 1000.0f;
    sim.pool.items[far].health = 1000.0f;
    uint32_t dead = sim.pool.items[a].id;
    sim_step(&sim);
    CHECK(!sim.pool.items[a].alive);
    CHECK(count_kind(&sim.pool.items[b], LIFE_KIN_LOST) == 1);
    CHECK(sim.pool.items[b].tally.kin_lost == 1);
    CHECK(count_kind(&sim.pool.items[far], LIFE_KIN_LOST) == 0);
    const NotableLife *n = chronicle_find(&sim.chronicle, dead);
    CHECK(n != NULL && !n->alive);
    if (n != NULL) {
        LifeEvent e = n->events[(n->events_noted - 1) % NOTABLE_EVENTS];
        CHECK(e.kind == LIFE_DIED && e.value == DEATH_HUNGER);
    }
    sim_free(&sim);
}

static void test_moved_once_settled(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    int c = spawn(&sim.pool, &sim.world, 8, 8);
    Creature *me = &sim.pool.items[c];
    me->energy = 1000.0f;
    me->health = 1000.0f;
    int cell = world_idx(&sim.world, 8, 8);
    sim.world.temperature[cell] = 1.0f; /* anything but cold */
    sim_step(&sim);
    CHECK(me->tally.home_region != REGION_COLD);
    sim.world.temperature[cell] = 0.0f; /* now cold, under its feet */
    for (int k = 0; k < REGION_SETTLE - 1; k++) {
        sim_step(&sim);
    }
    CHECK(count_kind(me, LIFE_MOVED) == 0);
    sim_step(&sim);
    CHECK(newest(me).kind == LIFE_MOVED && newest(me).value == REGION_COLD);
    CHECK(me->tally.moves == 1);
    sim_step(&sim);
    CHECK(me->tally.moves == 1);
    sim_free(&sim);
}

static void test_largest_oldest_and_giant_once(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    sim.set.giant_size = 2.0f;
    int big = spawn(&sim.pool, &sim.world, 3, 3);
    int old = spawn(&sim.pool, &sim.world, 10, 10);
    sim.pool.items[big].grown = 1.5f; /* size 2.5 */
    sim.pool.items[old].age = 500;
    for (int k = 0; k < 2; k++) {
        Creature *c = &sim.pool.items[k == 0 ? big : old];
        c->energy = 1000.0f;
        c->health = 1000.0f;
    }
    for (int k = 0; k < 3; k++) {
        sim_step(&sim);
    }
    const Creature *b = &sim.pool.items[big], *o = &sim.pool.items[old];
    CHECK(count_kind(b, LIFE_LARGEST) == 1);
    CHECK(count_kind(b, LIFE_GIANT) == 1);
    CHECK(count_kind(o, LIFE_OLDEST) == 1);
    CHECK(count_kind(o, LIFE_LARGEST) == 0 && count_kind(o, LIFE_GIANT) == 0);
    CHECK(b->tally.max_size >= 2.5f - 1e-4f);
    CHECK(strcmp(chronicle_title(&b->tally), "the Giant") == 0);
    sim_free(&sim);
}

static void test_ring_keeps_newest(void)
{
    Chronicle ch;
    CHECK(chronicle_init(&ch));
    Creature c = {.alive = true, .id = 3};
    for (uint32_t k = 0; k < 20; k++) {
        chronicle_note(&ch, &c, (LifeEvent){.tick = k, .kind = LIFE_ATTACKED});
    }
    CHECK(c.events_noted == 20);
    CHECK(newest(&c).tick == 19);
    /* The oldest kept is 20 - 8 = 12, at slot 12 % 8. */
    CHECK(c.events[12 % CREATURE_EVENTS].tick == 12);
    /* Nothing older is left anywhere in the ring. */
    for (int k = 0; k < CREATURE_EVENTS; k++) {
        CHECK(c.events[k].tick >= 12);
    }
    /* With no recorder, nothing happens. */
    chronicle_note(NULL, &c, (LifeEvent){.tick = 99, .kind = LIFE_ATTACKED});
    CHECK(c.events_noted == 20);
    chronicle_free(&ch);
}

static void test_table_replaces_least_notable(void)
{
    Chronicle ch;
    CHECK(chronicle_init(&ch));
    /* Fill the table: creature k has had k + 1 children. */
    for (uint32_t k = 0; k < CHRONICLE_NOTABLE; k++) {
        Creature c = {.alive = true, .id = k};
        for (uint32_t j = 0; j <= k; j++) {
            chronicle_note(&ch, &c, (LifeEvent){.kind = LIFE_CHILD});
        }
    }
    for (uint32_t k = 0; k < CHRONICLE_NOTABLE; k++) {
        CHECK(chronicle_find(&ch, k) != NULL);
    }
    /* One child is not above the least (also one): it does not get in. */
    Creature quiet = {.alive = true, .id = 1000};
    chronicle_note(&ch, &quiet, (LifeEvent){.kind = LIFE_CHILD});
    CHECK(chronicle_find(&ch, 1000) == NULL);
    /* A kill and a child (score 5) beat the least (1): creature 0 goes. */
    Creature bold = {.alive = true, .id = 2000};
    chronicle_note(&ch, &bold, (LifeEvent){.kind = LIFE_CHILD});
    chronicle_note(&ch, &bold, (LifeEvent){.kind = LIFE_KILL, .other_id = 9});
    const NotableLife *n = chronicle_find(&ch, 2000);
    CHECK(n != NULL);
    CHECK(chronicle_find(&ch, 0) == NULL);
    CHECK(chronicle_find(&ch, 1) != NULL);
    if (n != NULL) {
        /* It came in with both events from its own ring, oldest first. */
        CHECK(n->events_noted == 2);
        CHECK(n->events[0].kind == LIFE_CHILD && n->events[1].kind == LIFE_KILL);
        CHECK(n->tally.kills == 1);
    }
    chronicle_free(&ch);
}

static void test_recording_leaves_fingerprints_equal(void)
{
    Settings on = settings_default();
    Settings off = on;
    off.chronicle = 0;
    Sim a, b;
    CHECK(sim_init(&a, &on, 11, 64, 32));
    CHECK(sim_init(&b, &off, 11, 64, 32));
    bool equal = true;
    for (int k = 0; k < 3000; k++) {
        sim_step(&a);
        sim_step(&b);
        if (k % 100 == 0 && sim_checksum(&a) != sim_checksum(&b)) {
            equal = false;
        }
    }
    CHECK(equal);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    /* And the one recording did record. */
    int used = 0;
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        used += a.chronicle.lives[i].used;
    }
    CHECK(used == CHRONICLE_NOTABLE);
    used = 0;
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        used += b.chronicle.lives[i].used;
    }
    CHECK(used == 0);
    sim_free(&a);
    sim_free(&b);
}

/* Words that would claim a motive or an order of causes. */
static bool has_motive(const char *line)
{
    const char *banned[] = {"because", "after", "fled", "revenge"};
    for (size_t k = 0; k < sizeof(banned) / sizeof(banned[0]); k++) {
        if (strstr(line, banned[k]) != NULL) {
            return true;
        }
    }
    return false;
}

static void test_no_motive_words(void)
{
    LifeTally t = {.parents = {4, 5}};
    char line[128];
    for (int kind = 0; kind < LIFE_COUNT; kind++) {
        for (int value = 0; value < 6; value++) {
            LifeEvent e = {.tick = 1234, .kind = (uint8_t)kind, .other_id = 42,
                           .value = value};
            chronicle_line(&e, &t, 100, line, sizeof(line));
            CHECK(strncmp(line, "year 12: ", 9) == 0);
            CHECK(!has_motive(line));
            /* The panel's short form: the same facts, no motives, and within
             * the panel's 19 columns when the name is short. Names run from 4
             * to 14 letters, about 7 on average: id 42's is "Vudrainaeth", 11,
             * so the length is checked with id 27's, "Muna". */
            chronicle_short_line(&e, 100, line, sizeof(line));
            CHECK(strncmp(line, "y12 ", 4) == 0);
            CHECK(!has_motive(line));
            e.other_id = 27;
            chronicle_short_line(&e, 100, line, sizeof(line));
            CHECK(strlen(line) <= 19);
        }
    }
    /* And every line a real world wrote. */
    Settings set = settings_default();
    Sim sim;
    CHECK(sim_init(&sim, &set, 4, 64, 32));
    for (int k = 0; k < 1500; k++) {
        sim_step(&sim);
    }
    int lines = 0;
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        const NotableLife *n = &sim.chronicle.lives[i];
        int kept = n->events_noted < NOTABLE_EVENTS ? (int)n->events_noted
                                                    : NOTABLE_EVENTS;
        for (int k = 0; k < kept; k++) {
            chronicle_line(&n->events[k], &n->tally, 100, line, sizeof(line));
            CHECK(!has_motive(line));
            CHECK(strstr(line, "unrecorded") == NULL);
            lines++;
        }
    }
    CHECK(lines > 0);
    sim_free(&sim);
}

/* One creature in a bare world, well fed, that sleeps for the given ticks (the
 * script's one answer) and then rests, the script having run out. */
static int sleeper_for(Sim *sim, uint16_t ticks)
{
    int c = spawn(&sim->pool, &sim->world, 8, 8);
    Creature *me = &sim->pool.items[c];
    me->energy = 1000.0f;
    me->health = 1000.0f;
    Action script[] = {{.kind = ACT_SLEEP, .hold = ticks}};
    brain_script_load(&sim->brain, script, 1);
    return c;
}

/* A sleep is one event, noted on waking with its length, and only a sleep long
 * enough to matter: the 48-event ring must not fill with naps. */
static void test_one_event_per_sleep(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    Creature *me = &sim.pool.items[sleeper_for(&sim, 200)];
    for (int k = 0; k < 200; k++) {
        sim_step(&sim);
        CHECK(me->current.kind == ACT_SLEEP);
    }
    CHECK(me->sleep_ticks == 200);
    CHECK(count_kind(me, LIFE_SLEPT) == 0); /* not yet: it is still asleep */
    sim_step(&sim); /* the script is over: it rests, awake */
    CHECK(me->current.kind == ACT_REST);
    CHECK(me->sleep_ticks == 0);
    CHECK(count_kind(me, LIFE_SLEPT) == 1);
    CHECK(newest(me).kind == LIFE_SLEPT && newest(me).value == 200);
    for (int k = 0; k < 10; k++) {
        sim_step(&sim);
    }
    CHECK(count_kind(me, LIFE_SLEPT) == 1); /* once */
    sim_free(&sim);

    /* A nap, or one just short of SLEEP_NOTED, is not noted; SLEEP_NOTED is.
     * Naps need the minimum sleep turned off: with it, no sleep is that short. */
    const uint16_t lengths[] = {10, SLEEP_NOTED - 1, SLEEP_NOTED};
    for (int n = 0; n < 3; n++) {
        make_sim(&sim, &set);
        sim.set.nap_min_ticks = 0;
        me = &sim.pool.items[sleeper_for(&sim, lengths[n])];
        for (int k = 0; k <= lengths[n]; k++) {
            sim_step(&sim);
        }
        CHECK(me->current.kind == ACT_REST && me->sleep_ticks == 0);
        CHECK(count_kind(me, LIFE_SLEPT) == (lengths[n] >= SLEEP_NOTED));
        sim_free(&sim);
    }

    /* Dying asleep: the death is noted, and no sleep. */
    make_sim(&sim, &set);
    me = &sim.pool.items[sleeper_for(&sim, 300)];
    uint32_t id = me->id;
    for (int k = 0; k < 100; k++) {
        sim_step(&sim);
    }
    me->energy = 0.0f;
    me->health = 0.001f; /* hunger ends it on the next tick */
    sim_step(&sim);
    CHECK(!me->alive);
    const NotableLife *life = chronicle_find(&sim.chronicle, id);
    CHECK(life != NULL);
    if (life != NULL) {
        LifeEvent last = life->events[(life->events_noted - 1) % NOTABLE_EVENTS];
        CHECK(last.kind == LIFE_DIED && last.value == DEATH_HUNGER);
        for (uint32_t k = 0; k < life->events_noted && k < NOTABLE_EVENTS; k++) {
            CHECK(life->events[k].kind != LIFE_SLEPT);
        }
    }
    sim_free(&sim);
}

/* What a sleep becomes. Outside winter, a nap: it lasts at least nap_min_ticks
 * and pays what resting pays. In winter, with stores of hibernate_energy times
 * what mating needs, a hibernation: it lasts until the first tick of spring,
 * whatever the brain asked, and pays sleep_upkeep's small share. In winter
 * without the stores, only a nap. */
static void test_naps_and_hibernation(void)
{
    Sim sim;
    Settings set;
    make_sim(&sim, &set);
    CHECK(sim.set.nap_min_ticks == 10);
    Creature *me = &sim.pool.items[sleeper_for(&sim, 1)]; /* tick 0: spring */
    for (int k = 0; k < 10; k++) {
        sim_step(&sim);
        CHECK(me->current.kind == ACT_SLEEP && me->hibernating == 0);
    }
    sim_step(&sim); /* the nap is over, and so is the script: it rests */
    CHECK(me->current.kind == ACT_REST);
    sim_free(&sim);

    /* A nap pays rest's share of upkeep: the same as resting, nothing saved. */
    make_sim(&sim, &set);
    me = &sim.pool.items[sleeper_for(&sim, 1)];
    float before = me->energy;
    sim_step(&sim);
    float want = creature_upkeep_cost(me, &sim.set) * sim.set.rest_upkeep;
    CHECK(fabsf((before - me->energy) - want) < 1e-3f);
    sim_free(&sim);

    /* Winter, well fed: asleep from the first tick of winter to the last. */
    int year = set.ticks_per_year;
    make_sim(&sim, &set);
    sim.tick = (uint64_t)(year * 3 / 4); /* the first tick of winter */
    me = &sim.pool.items[sleeper_for(&sim, 1)];
    CHECK(creature_can_hibernate(me, &sim.set));
    for (int k = 0; k < year / 4; k++) {
        float e = me->energy;
        sim_step(&sim);
        CHECK(me->current.kind == ACT_SLEEP && me->hibernating == 1);
        if (k == 0) {
            float cheap = creature_upkeep_cost(me, &sim.set) * sim.set.sleep_upkeep;
            CHECK(fabsf((e - me->energy) - cheap) < 1e-3f);
        }
    }
    CHECK(sim.tick == (uint64_t)year); /* spring has come */
    sim_step(&sim); /* spring's first turn: awake */
    CHECK(me->current.kind == ACT_REST && me->hibernating == 0);
    sim_free(&sim);

    /* Winter, with too little stored: only a nap. */
    make_sim(&sim, &set);
    sim.tick = (uint64_t)(year * 3 / 4);
    me = &sim.pool.items[sleeper_for(&sim, 1)];
    me->energy = 1.0f;
    CHECK(!creature_can_hibernate(me, &sim.set));
    for (int k = 0; k < 10; k++) {
        sim_step(&sim);
        CHECK(me->current.kind == ACT_SLEEP && me->hibernating == 0);
    }
    sim_step(&sim);
    CHECK(me->current.kind == ACT_REST);
    sim_free(&sim);
}

static void test_lines_read_right(void)
{
    LifeTally t = {.parents = {4, 5}};
    char line[128], a[16], b[16];
    name_from_id(4, a, sizeof(a));
    name_from_id(5, b, sizeof(b));
    char want[128];
    LifeEvent born = {.tick = 250, .kind = LIFE_BORN, .other_id = 4, .value = 1};
    chronicle_line(&born, &t, 100, line, sizeof(line));
    snprintf(want, sizeof(want), "year 2: born to %s and %s", a, b);
    CHECK(strcmp(line, want) == 0);
    LifeEvent moved = {.tick = 0, .kind = LIFE_MOVED, .value = REGION_COLD};
    chronicle_line(&moved, &t, 100, line, sizeof(line));
    CHECK(strcmp(line, "year 0: moved into the cold lands") == 0);
    LifeEvent big = {.tick = 0, .kind = LIFE_GIANT, .value = 63};
    chronicle_line(&big, &t, 100, line, sizeof(line));
    CHECK(strcmp(line, "year 0: grew to giant size, 6.3") == 0);
    /* A sleep says how long, in years, and the season it ended in. */
    LifeEvent slept = {.tick = 250, .kind = LIFE_SLEPT, .value = 30};
    chronicle_line(&slept, &t, 100, line, sizeof(line));
    CHECK(strcmp(line, "year 2: slept 0.3 y, woke in autumn") == 0);
    chronicle_short_line(&slept, 100, line, sizeof(line));
    CHECK(strcmp(line, "y2 slept 0.3 y") == 0);
    slept.value = 5; /* under a tenth of a year */
    chronicle_line(&slept, &t, 100, line, sizeof(line));
    CHECK(strcmp(line, "year 2: slept under 0.1 y, woke in autumn") == 0);
    chronicle_short_line(&slept, 100, line, sizeof(line));
    CHECK(strcmp(line, "y2 slept <0.1 y") == 0);
    /* A short buffer is cut, never overrun. */
    char tiny[8];
    chronicle_line(&big, &t, 100, tiny, sizeof(tiny));
    CHECK(strlen(tiny) == 7);
}

static void test_names_from_ids(void)
{
    char a[16], b[16];
    name_from_id(12345, a, sizeof(a));
    name_from_id(12345, b, sizeof(b));
    CHECK(strcmp(a, b) == 0); /* the same id, the same name */
    CHECK(strlen(a) >= 2);
    CHECK(a[0] >= 'A' && a[0] <= 'Z');
    /* Different ids mostly give different names. */
    int same = 0;
    for (uint32_t id = 0; id < 200; id++) {
        name_from_id(id, a, sizeof(a));
        name_from_id(id + 1, b, sizeof(b));
        CHECK(strlen(a) < sizeof(a));
        same += strcmp(a, b) == 0;
    }
    CHECK(same < 10);
    char tiny[3];
    name_from_id(7, tiny, sizeof(tiny));
    CHECK(strlen(tiny) <= 2);
}

static void test_titles_from_thresholds(void)
{
    LifeTally t = {0};
    CHECK(strcmp(chronicle_title(&t), "") == 0);
    t.moves = 3;
    CHECK(strcmp(chronicle_title(&t), "the Wanderer") == 0);
    t.kills = 10;
    CHECK(strcmp(chronicle_title(&t), "the Slayer") == 0);
    t.hunter_kills = 3;
    CHECK(strcmp(chronicle_title(&t), "Breaker of Hunters") == 0);
    t.flags |= TALLY_GIANT;
    CHECK(strcmp(chronicle_title(&t), "the Giant") == 0);
}

static void test_memory_flat_with_churn(void)
{
    /* The table is allocated once, in sim_init; a churning world must never
     * grow or move it. Whether anything else allocates is the memory checkers'
     * job, run over this same program. */
    Settings set = settings_default();
    /* Churn needs births; this small world's few plants would leave the
     * plants' capacity below its founders, so the birth rule is off here. */
    set.birth_per_plant = 0.0f;
    Sim sim;
    CHECK(sim_init(&sim, &set, 9, 48, 24));
    const NotableLife *table = sim.chronicle.lives;
    uint32_t born_before = sim.pool.next_id;
    for (int k = 0; k < 2000; k++) {
        sim_step(&sim);
    }
    CHECK(sim.chronicle.lives == table);
    CHECK(sim.pool.next_id > born_before); /* births happened: slots churned */
    sim_free(&sim);
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_founders_are_born_founders();
    test_birth_names_parents_and_children();
    test_first_hit_of_a_fight_is_noted_once();
    test_heavy_wound_noted_when_crossed();
    test_kills_counted_and_hunters_named();
    test_death_by_wounds_names_the_killer();
    test_survived_fight_after_quiet();
    test_kin_lost_nearby();
    test_moved_once_settled();
    test_one_event_per_sleep();
    test_naps_and_hibernation();
    test_largest_oldest_and_giant_once();
    test_ring_keeps_newest();
    test_table_replaces_least_notable();
    test_recording_leaves_fingerprints_equal();
    test_no_motive_words();
    test_lines_read_right();
    test_names_from_ids();
    test_titles_from_thresholds();
    test_memory_flat_with_churn();
    TEST_SUMMARY();
}
