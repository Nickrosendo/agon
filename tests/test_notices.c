#include "test.h"

#include <string.h> /* strstr */

#include "actions.h"
#include "genome.h"
#include "notices.h"
#include "settings.h"
#include "sim.h"

/* notices_check only looks when a new sample has been taken. The tests move
 * the sample's tick by hand, so they can change the world between checks
 * without stepping it. */
static void next_sample(Sim *s)
{
    s->tick += (uint64_t)s->set.stats_every;
    s->stats.tick = s->tick;
}

static int first_living(const Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            return i;
        }
    }
    return -1;
}

/* Puts every living creature's watched genes at 0.2, so no body stands out
 * until a test makes one. */
static void plain_bodies(Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        Creature *c = &s->pool.items[i];
        for (int k = 0; k < NOTICE_GENES; k++) {
            c->genome.g[NOTICE_GENE_LIST[k]] = 0.2f;
        }
    }
}

/* Renames the first size living creatures into one family, the first one's;
 * returns that creature's slot. */
static int make_family(Sim *s, int size)
{
    int first = -1, members = 0;
    for (int i = 0; i < s->pool.capacity && members < size; i++) {
        Creature *c = &s->pool.items[i];
        if (c->alive) {
            first = first < 0 ? i : first;
            c->lineage = s->pool.items[first].lineage;
            members++;
        }
    }
    return members == size ? first : -1;
}

static bool newest_has(const Notices *n, const char *text)
{
    const Notice *x = notices_newest(n);
    return x != NULL && strstr(x->text, text) != NULL;
}

/* What is already in the world at the first check is not news. */
static void test_first_check_is_quiet(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    CHECK(n.total == 0);
    sim_free(&s);
}

/* A body unlike its family is told once, not again at the next sample, and
 * again only after no such body has lived for a year. */
static void test_unlike_body_once(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    plain_bodies(&s);
    int slot = make_family(&s, 12);
    CHECK(slot >= 0);
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    Creature *c = &s.pool.items[slot];
    c->genome.g[GENE_ARMOUR] = 0.95f; /* the family's average: about 0.26 */
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    CHECK(newest_has(&n, "armour"));
    CHECK(notices_newest(&n)->creature == c->id);
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1); /* the same body, still there: not news */
    /* Gone for less than a year, back: still not news. */
    c->genome.g[GENE_ARMOUR] = 0.2f;
    next_sample(&s);
    notices_check(&n, &s);
    c->genome.g[GENE_ARMOUR] = 0.95f;
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    /* Gone for a whole year, back: news again. */
    c->genome.g[GENE_ARMOUR] = 0.2f;
    next_sample(&s);
    notices_check(&n, &s); /* the last one went at this check */
    s.tick += (uint64_t)set.ticks_per_year;
    s.stats.tick = s.tick;
    notices_check(&n, &s);
    c->genome.g[GENE_ARMOUR] = 0.95f;
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 2);
    sim_free(&s);
}

/* A small family has no average worth comparing with. */
static void test_small_family_is_quiet(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    plain_bodies(&s);
    int slot = make_family(&s, NOTICE_LIKE_FAMILY - 1);
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    s.pool.items[slot].genome.g[GENE_ARMOUR] = 1.0f;
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 0);
    sim_free(&s);
}

/* Many unlike bodies at once is a family splitting, not news. */
static void test_many_unlike_is_quiet(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    plain_bodies(&s);
    CHECK(make_family(&s, 20) >= 0);
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    int made = 0;
    for (int i = 0; i < s.pool.capacity && made <= NOTICE_RARE; i++) {
        if (s.pool.items[i].alive) {
            s.pool.items[i].genome.g[GENE_SIZE] = 1.0f; /* average 0.36 */
            made++;
        }
    }
    CHECK(made == NOTICE_RARE + 1);
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 0);
    sim_free(&s);
}

static void test_giant_once(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    int slot = first_living(&s);
    s.chronicle.giants++;
    s.chronicle.newest_giant = s.pool.items[slot].id;
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    CHECK(newest_has(&n, "first giant"));
    CHECK(notices_newest(&n)->creature == s.pool.items[slot].id);
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    /* A second giant no larger than the record: not news. */
    int other = slot + 1;
    while (!s.pool.items[other].alive) {
        other++;
    }
    s.chronicle.giants++;
    s.chronicle.newest_giant = s.pool.items[other].id;
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    /* A body far larger than the record: news, once. */
    s.pool.items[other].grown += 50.0f;
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 2);
    CHECK(newest_has(&n, "largest yet"));
    CHECK(notices_newest(&n)->creature == s.pool.items[other].id);
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 2);
    sim_free(&s);
}

static void test_tactic_once(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    s.tactics.seen |= (uint16_t)(1u << TACTIC_HUDDLE);
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    CHECK(newest_has(&n, tactics_name(TACTIC_HUDDLE)));
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    sim_free(&s);
}

/* A family of NOTICE_FAMILY_MIN or more whose last member is gone. */
static void test_family_gone_once(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    plain_bodies(&s); /* random bodies in one family would stand out */
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    int slot = make_family(&s, NOTICE_FAMILY_MIN);
    CHECK(slot >= 0);
    uint32_t family = s.pool.items[slot].lineage;
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 0); /* growing big is not news */
    /* Now none of them carries the name. */
    for (int i = 0; i < s.pool.capacity; i++) {
        if (s.pool.items[i].lineage == family) {
            s.pool.items[i].lineage = family + 100000;
        }
    }
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    CHECK(newest_has(&n, "family is gone"));
    CHECK(notices_newest(&n)->creature == NOTICE_NOBODY);
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    sim_free(&s);
}

/* A given trait past NOTICE_GIFT_SPREAD carriers is told once; so is its end. */
static void test_given_trait_spread_and_gone(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    plain_bodies(&s);
    Notices n;
    notices_reset(&n);
    notices_check(&n, &s);
    int slot = first_living(&s);
    CHECK(actions_give_trait(&s, s.pool.items[slot].id, GENE_SWIM, 0.8f));
    sim_step(&s); /* applies it */
    /* Pretend it spread: eleven more carry it. */
    int carrying = 0;
    for (int i = 0; i < s.pool.capacity && carrying < NOTICE_GIFT_SPREAD + 1; i++) {
        if (s.pool.items[i].alive) {
            s.pool.items[i].genome.g[GENE_SWIM] = 0.8f;
            carrying++;
        }
    }
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    CHECK(newest_has(&n, "swimming"));
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 1);
    for (int i = 0; i < s.pool.capacity; i++) {
        s.pool.items[i].genome.g[GENE_SWIM] = 0.2f;
    }
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 2);
    CHECK(newest_has(&n, "died out"));
    next_sample(&s);
    notices_check(&n, &s);
    CHECK(n.total == 2);
    sim_free(&s);
}

/* Checking reads the world and changes nothing in it. */
static void test_checking_changes_nothing(void)
{
    Settings set = settings_default();
    Sim a, b;
    CHECK(sim_init(&a, &set, 4, 64, 32));
    CHECK(sim_init(&b, &set, 4, 64, 32));
    Notices n;
    notices_reset(&n);
    for (int t = 0; t < set.ticks_per_year; t++) {
        sim_step(&a);
        notices_check(&n, &a);
        sim_step(&b);
    }
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    CHECK(sim_story_checksum(&a) == sim_story_checksum(&b));
    sim_free(&a);
    sim_free(&b);
}

/* The ring keeps the newest NOTICES_KEPT; notices_get counts back from the
 * newest. */
static void test_ring_keeps_newest(void)
{
    Notices n;
    notices_reset(&n);
    for (int i = 0; i < NOTICES_KEPT + 5; i++) {
        notices_add(&n, (uint64_t)i, (uint32_t)i, "x");
    }
    CHECK(n.total == NOTICES_KEPT + 5);
    CHECK(notices_get(&n, 0)->tick == NOTICES_KEPT + 4);
    CHECK(notices_get(&n, NOTICES_KEPT - 1)->tick == 5);
    CHECK(notices_get(&n, NOTICES_KEPT) == NULL);
}

int main(void)
{
    test_first_check_is_quiet();
    test_unlike_body_once();
    test_small_family_is_quiet();
    test_many_unlike_is_quiet();
    test_giant_once();
    test_tactic_once();
    test_family_gone_once();
    test_given_trait_spread_and_gone();
    test_checking_changes_nothing();
    test_ring_keeps_newest();
    TEST_SUMMARY();
}
