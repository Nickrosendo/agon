#include "test.h"

#include <string.h> /* strstr */

#include "actions.h"
#include "branch.h"
#include "genome.h"
#include "settings.h"
#include "sim.h"

/* The first living creature's slot, or -1. */
static int first_alive(const Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            return i;
        }
    }
    return -1;
}

/* Runs the shown world n ticks, then brings the other one level with it. */
static void step_both(Branches *b, Sim *shown, int n)
{
    for (int i = 0; i < n; i++) {
        sim_step(shown);
    }
    branch_catch_up_all(b, shown);
}

/* A branch is the same world twice: stepped alike they stay equal. A trait
 * given in one of them alone sets them apart. */
static void test_branch_equal_then_apart(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 6, 64, 32));
    for (int i = 0; i < 50; i++) {
        sim_step(&s);
    }
    Branches b = {0};
    char err[128];
    CHECK(branch_make(&b, &s, err, sizeof(err)));
    CHECK(b.split && b.shown == 0 && b.split_at == 50);
    CHECK(!branch_make(&b, &s, err, sizeof(err))); /* only two at a time */
    CHECK(strstr(err, "already") != NULL);

    step_both(&b, &s, 200);
    CHECK(b.other.tick == s.tick);
    CHECK(sim_checksum(&b.other) == sim_checksum(&s));
    CHECK(sim_story_checksum(&b.other) == sim_story_checksum(&s));

    /* A trait in the shown world only: from the next tick on they differ. */
    int slot = first_alive(&s);
    CHECK(slot >= 0);
    CHECK(actions_give_trait(&s, s.pool.items[slot].id, GENE_ARMOUR, 1.0f));
    step_both(&b, &s, 200);
    CHECK(b.other.tick == s.tick);
    CHECK(sim_checksum(&b.other) != sim_checksum(&s));
    branch_free(&b);
    CHECK(!b.split);
    sim_free(&s);
}

/* Switching swaps the two whole worlds; switching twice gives back the first. */
static void test_switch_swaps(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 6, 64, 32));
    Branches b = {0};
    char err[128];
    CHECK(branch_make(&b, &s, err, sizeof(err)));
    int slot = first_alive(&s);
    CHECK(actions_give_trait(&s, s.pool.items[slot].id, GENE_ARMOUR, 1.0f));
    step_both(&b, &s, 100);
    uint64_t a_sum = sim_checksum(&s), b_sum = sim_checksum(&b.other);
    CHECK(a_sum != b_sum);
    CHECK(branch_switch(&b, &s));
    CHECK(b.shown == 1);
    CHECK(sim_checksum(&s) == b_sum && sim_checksum(&b.other) == a_sum);
    CHECK(branch_switch(&b, &s));
    CHECK(b.shown == 0 && sim_checksum(&s) == a_sum);
    branch_free(&b);
    sim_free(&s);
}

/* A branch behind the shown world cannot be switched to until it catches up,
 * so the two are always compared at the same moment. */
static void test_no_switch_while_behind(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 6, 64, 32));
    Branches b = {0};
    char err[128];
    CHECK(branch_make(&b, &s, err, sizeof(err)));
    sim_step(&s);
    CHECK(!branch_caught_up(&b, &s));
    CHECK(!branch_switch(&b, &s));
    CHECK(b.shown == 0);
    branch_catch_up_all(&b, &s);
    CHECK(branch_caught_up(&b, &s));
    CHECK(branch_switch(&b, &s));
    branch_free(&b);
    sim_free(&s);
}

/* Keeping either side leaves one world that runs on, and nothing leaks (the
 * memory checkers would report the other world otherwise). */
static void test_keep_either_side(void)
{
    Settings set = settings_default();
    for (int side = 0; side < 2; side++) {
        Sim s;
        CHECK(sim_init(&s, &set, 6, 64, 32));
        Branches b = {0};
        char err[128];
        CHECK(branch_make(&b, &s, err, sizeof(err)));
        int slot = first_alive(&s);
        CHECK(actions_give_trait(&s, s.pool.items[slot].id, GENE_ARMOUR, 1.0f));
        step_both(&b, &s, 50);
        uint64_t want = side == 0 ? sim_checksum(&s) : sim_checksum(&b.other);
        if (side == 1) {
            CHECK(branch_switch(&b, &s));
        }
        branch_keep_shown(&b);
        CHECK(!b.split);
        CHECK(sim_checksum(&s) == want);
        for (int i = 0; i < 50; i++) {
            sim_step(&s); /* the kept world still runs */
        }
        CHECK(s.tick == 100);
        branch_free(&b); /* nothing left to free: does nothing */
        sim_free(&s);
    }
}

/* The other branch stops catching up when its world has ended, so a dead
 * branch cannot hold the window up. */
static void test_dead_branch_counts_as_caught_up(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 6, 64, 32));
    Branches b = {0};
    char err[128];
    CHECK(branch_make(&b, &s, err, sizeof(err)));
    for (int i = 0; i < b.other.pool.capacity; i++) {
        b.other.pool.items[i].alive = false;
    }
    b.other.pool.count = 0;
    sim_step(&s);
    CHECK(branch_caught_up(&b, &s));
    char text[160];
    branch_status_text(&b, &s, text, sizeof(text));
    CHECK(strstr(text, "ended") != NULL);
    branch_free(&b);
    sim_free(&s);
}

int main(void)
{
    test_branch_equal_then_apart();
    test_switch_swaps();
    test_no_switch_while_behind();
    test_keep_either_side();
    test_dead_branch_counts_as_caught_up();
    TEST_SUMMARY();
}
