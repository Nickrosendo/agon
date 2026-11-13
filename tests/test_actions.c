#include "test.h"

#include <math.h> /* NAN */

#include "actions.h"
#include "genome.h"
#include "save.h"
#include "settings.h"
#include "sim.h"

#define SAVE_FILE "build/test_actions.save"

static void run_to(Sim *s, uint64_t tick)
{
    while (s->tick < tick) {
        sim_step(s);
    }
}

/* No creature has this id: ids count up from 0, and no test world gets near. */
#define NOBODY 0xFFFFFFFFu

/* The id of some living creature, or NOBODY when none lives. */
static uint32_t some_living(const Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            return s->pool.items[i].id;
        }
    }
    return NOBODY;
}

static PlayerAction trait(uint64_t tick, uint32_t id, int gene, float value)
{
    return (PlayerAction){.tick = tick, .kind = ACTION_TRAIT,
                          .gene = (uint8_t)gene, .creature = id,
                          .value = value};
}

static void test_add_keeps_order(void)
{
    ActionLog log = {0};
    CHECK(actions_add(&log, trait(9, 1, GENE_SIZE, 0.5f)));
    CHECK(!actions_add(&log, trait(5, 1, GENE_SIZE, 0.5f)));
    CHECK(actions_add(&log, trait(9, 2, GENE_SIZE, 0.5f))); /* same tick is fine */
    /* Past the first 16 entries, so the log has to grow at least once. */
    for (int i = 0; i < 40; i++) {
        CHECK(actions_add(&log, trait(10 + (uint64_t)i, 1, GENE_SIZE, 0.1f)));
    }
    CHECK(log.count == 42);
    actions_seek(&log, 12);
    CHECK(log.items[log.next].tick == 12);
    actions_free(&log);
    CHECK(log.items == NULL && log.count == 0);
}

/* A live action and the same action pre-filled in the log of a fresh run give
 * the same world: the heart of replaying. */
static void test_replay_same_checksum(void)
{
    Settings set = settings_default();
    Sim live, replay;
    CHECK(sim_init(&live, &set, 7, 64, 32));
    CHECK(sim_init(&replay, &set, 7, 64, 32));
    run_to(&live, 100);
    uint32_t id = some_living(&live);
    CHECK(id != NOBODY);
    PlayerAction a = trait(live.tick, id, GENE_SIZE, 1.0f);
    CHECK(actions_add(&live.actions, a));
    CHECK(actions_add(&replay.actions, a));
    run_to(&live, 400);
    run_to(&replay, 400);
    CHECK(sim_checksum(&live) == sim_checksum(&replay));
    CHECK(sim_story_checksum(&live) == sim_story_checksum(&replay));

    /* And the action did something: a run without it differs. */
    Sim plain;
    CHECK(sim_init(&plain, &set, 7, 64, 32));
    run_to(&plain, 400);
    CHECK(sim_checksum(&plain) != sim_checksum(&live));
    sim_free(&live);
    sim_free(&replay);
    sim_free(&plain);
}

/* An action for a creature no one is (already dead, or never born) does
 * nothing, and the log entry stays for a replay to meet. */
static void test_dead_target_is_noop(void)
{
    Settings set = settings_default();
    Sim with, without;
    CHECK(sim_init(&with, &set, 7, 64, 32));
    CHECK(sim_init(&without, &set, 7, 64, 32));
    CHECK(actions_add(&with.actions, trait(50, NOBODY, GENE_SIZE, 1.0f)));
    run_to(&with, 200);
    run_to(&without, 200);
    CHECK(sim_checksum(&with) == sim_checksum(&without));
    CHECK(with.actions.count == 1 && with.actions.next == 1);
    sim_free(&with);
    sim_free(&without);
}

/* A save with an action still to come keeps it, and the loaded world applies
 * it on the same tick. */
static void test_save_keeps_log(void)
{
    Settings set = settings_default();
    Sim a;
    CHECK(sim_init(&a, &set, 7, 64, 32));
    run_to(&a, 50);
    uint32_t id = some_living(&a);
    CHECK(actions_add(&a.actions, trait(20, id, GENE_ARMOUR, 0.9f))); /* past */
    CHECK(actions_add(&a.actions, trait(100, id, GENE_ARMOUR, 1.0f)));
    a.actions.next = 1; /* the first already happened, as if made then */
    char err[256];
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    Sim b;
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(b.actions.count == 2 && b.actions.next == 1);
    CHECK(b.actions.items[1].creature == id);
    CHECK(b.actions.items[1].value == 1.0f);
    run_to(&a, 200);
    run_to(&b, 200);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    sim_free(&a);
    sim_free(&b);
    remove(SAVE_FILE);
}

/* A new seed (the r key) starts with an empty log. */
static void test_reseed_clears(void)
{
    Settings set = settings_default();
    Sim a;
    CHECK(sim_init(&a, &set, 7, 64, 32));
    CHECK(actions_add(&a.actions, trait(5, 1, GENE_SIZE, 1.0f)));
    CHECK(sim_reseed(&a, 8));
    CHECK(a.actions.count == 0 && a.actions.next == 0);
    sim_free(&a);
}

/* Giving a trait changes that one gene and nothing else of the genome. */
static void test_trait_changes_only_that_gene(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 9, 64, 32));
    uint32_t id = some_living(&s);
    int idx = -1;
    for (int i = 0; i < s.pool.capacity; i++) {
        if (s.pool.items[i].alive && s.pool.items[i].id == id) {
            idx = i;
        }
    }
    CHECK(idx >= 0);
    Genome before = s.pool.items[idx].genome;
    CHECK(actions_give_trait(&s, id, GENE_ARMOUR, 1.0f));
    /* Applied at the start of the tick, before the creature can die. */
    actions_apply_due(&s);
    const Genome *after = &s.pool.items[idx].genome;
    int differ = 0;
    for (int i = 0; i < GENOME_LEN; i++) {
        differ += after->g[i] != before.g[i];
    }
    CHECK(differ == (before.g[GENE_ARMOUR] != 1.0f));
    CHECK(after->g[GENE_ARMOUR] == 1.0f);
    CHECK(s.pool.items[idx].health <=
          creature_max_health(&s.pool.items[idx], &s.set));
    sim_free(&s);
}

/* Brain genes, values outside 0..1, NaN and the dead are refused, and nothing
 * goes into the log for them. */
static void test_trait_refusals(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 9, 64, 32));
    uint32_t id = some_living(&s);
    CHECK(!actions_give_trait(&s, id, GENE_SURE, 0.5f));
    CHECK(!actions_give_trait(&s, id, GENE_TRAITS, 0.5f)); /* a weight */
    CHECK(!actions_give_trait(&s, id, GENE_SIZE, 1.5f));
    CHECK(!actions_give_trait(&s, id, GENE_SIZE, -0.1f));
    CHECK(!actions_give_trait(&s, id, GENE_SIZE, NAN)); /* math.h's NaN */
    CHECK(!actions_give_trait(&s, NOBODY, GENE_SIZE, 0.5f));
    CHECK(s.actions.count == 0);
    for (int i = 0; i < ACTIONS_GIVABLE_COUNT; i++) {
        CHECK(actions_give_trait(&s, id, ACTIONS_GIVABLE[i], 0.5f));
    }
    CHECK(s.actions.count == ACTIONS_GIVABLE_COUNT);
    sim_free(&s);
}

/* A given trait is inherited: every creature gets it, mutation is off and no
 * new founders are dropped in, so every child born afterwards must carry the
 * exact value, from both parents. */
static void test_trait_inherited(void)
{
    Settings set = settings_default();
    set.mutation_rate = 0.0f;
    set.min_creatures = 0; /* no restocking with random genomes */
    Sim s;
    CHECK(sim_init(&s, &set, 9, 96, 48));
    for (int i = 0; i < s.pool.capacity; i++) {
        if (s.pool.items[i].alive) {
            CHECK(actions_give_trait(&s, s.pool.items[i].id, GENE_LEGS, 0.8125f));
        }
    }
    uint32_t first_child = s.pool.next_id; /* ids after this are born later */
    run_to(&s, (uint64_t)(2 * set.ticks_per_year));
    int born = 0, carry = 0;
    for (int i = 0; i < s.pool.capacity; i++) {
        const Creature *c = &s.pool.items[i];
        if (c->alive && c->id >= first_child) {
            born++;
            carry += c->genome.g[GENE_LEGS] == 0.8125f;
        }
    }
    CHECK(born > 0);
    CHECK(carry == born);
    sim_free(&s);
}

int main(void)
{
    test_add_keeps_order();
    test_replay_same_checksum();
    test_dead_target_is_noop();
    test_save_keeps_log();
    test_reseed_clears();
    test_trait_changes_only_that_gene();
    test_trait_refusals();
    test_trait_inherited();
    TEST_SUMMARY();
}
