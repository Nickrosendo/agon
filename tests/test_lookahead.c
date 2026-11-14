#include "test.h"

#include <string.h> /* strstr, strlen */

#include "actions.h"
#include "creature.h"
#include "genome.h"
#include "lifepage.h" /* life_find_slot */
#include "lookahead.h"
#include "settings.h"
#include "sim.h"

/* The look-ahead in these tests: shorter than the window's season (150 ticks)
 * because the worlds run under the memory checkers, which are slow. */
#define LEAD 60

static int first_alive(const Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            return i;
        }
    }
    return -1;
}

/* Watching is free: two worlds from the same seed, one with a look-ahead
 * kept level with it and asked for a pick every tick, end the same. */
static void test_watching_is_free(void)
{
    Settings set = settings_default();
    Sim watched, plain;
    CHECK(sim_init(&watched, &set, 9, 64, 32));
    CHECK(sim_init(&plain, &set, 9, 64, 32));
    Lookahead l;
    lookahead_init(&l, LEAD);
    char why[LOOKAHEAD_WHY];
    for (int t = 0; t < 200; t++) {
        lookahead_sync_all(&l, &watched);
        int reason;
        (void)lookahead_pick(&l, &watched, 0, &reason, why, sizeof(why));
        sim_step(&watched);
        sim_step(&plain);
    }
    CHECK(sim_checksum(&watched) == sim_checksum(&plain));
    CHECK(sim_story_checksum(&watched) == sim_story_checksum(&plain));
    lookahead_free(&l);
    sim_free(&watched);
    sim_free(&plain);
}

/* The copy runs exactly lead ticks ahead once synced, and it is the real
 * future: the shown world, stepped that far, meets the copy's fingerprint. */
static void test_copy_is_the_future(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 4, 64, 32));
    Lookahead l;
    lookahead_init(&l, LEAD);
    CHECK(!lookahead_ready(&l, &s));
    lookahead_sync_all(&l, &s);
    CHECK(lookahead_ready(&l, &s));
    CHECK(l.ahead.tick == s.tick + LEAD);
    uint64_t future = sim_checksum(&l.ahead);
    for (int t = 0; t < LEAD; t++) {
        sim_step(&s);
    }
    CHECK(sim_checksum(&s) == future);
    lookahead_free(&l);
    sim_free(&s);
}

/* The pick is a creature alive in the shown world, with a reason; before
 * the copy is ready there is no pick. */
static void test_pick_is_alive(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 2, 64, 32));
    for (int t = 0; t < 100; t++) {
        sim_step(&s);
    }
    Lookahead l;
    lookahead_init(&l, LEAD);
    char why[LOOKAHEAD_WHY];
    int reason;
    CHECK(lookahead_pick(&l, &s, 0, &reason, why, sizeof(why)) ==
          LOOKAHEAD_NOBODY);
    lookahead_sync_all(&l, &s);
    uint32_t id = lookahead_pick(&l, &s, 0, &reason, why, sizeof(why));
    CHECK(id != LOOKAHEAD_NOBODY);
    CHECK(life_find_slot(&s, id) >= 0);
    CHECK(strlen(why) > 0);
    lookahead_free(&l);
    sim_free(&s);
}

/* An action in the shown world makes the copy's future wrong: the next sync
 * starts a new copy, and it is not ready until it has run ahead again. */
static void test_action_makes_a_new_copy(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 5, 64, 32));
    Lookahead l;
    lookahead_init(&l, LEAD);
    lookahead_sync_all(&l, &s);
    CHECK(lookahead_ready(&l, &s));
    int slot = first_alive(&s);
    CHECK(slot >= 0);
    CHECK(actions_give_trait(&s, s.pool.items[slot].id, GENE_ARMOUR, 1.0f));
    CHECK(!lookahead_ready(&l, &s));
    lookahead_sync_all(&l, &s);
    CHECK(lookahead_ready(&l, &s));
    /* The new copy carries the gift: it is the future of the changed world. */
    uint64_t future = sim_checksum(&l.ahead);
    for (int t = 0; t < LEAD; t++) {
        sim_step(&s);
    }
    CHECK(sim_checksum(&s) == future);
    lookahead_free(&l);
    sim_free(&s);
}

/* The score: action first. A hunter killed beats a first kill, which beats
 * growing giant, which beats a child; nothing coming scores least. */
static void test_score_order(void)
{
    LifeTally now = {0};
    LifeTally child = now, kill = now, giant = now, hunter = now;
    child.children = 1;
    kill.kills = 1;
    giant.flags = TALLY_GIANT;
    hunter.kills = 1;
    hunter.hunter_kills = 1;
    int why = -1;
    float s_none = lookahead_score(&now, 100, &now, 250, &why);
    CHECK(why == LOOK_QUIET);
    float s_child = lookahead_score(&now, 100, &child, 250, &why);
    CHECK(strstr(lookahead_reason_text(why), "young") != NULL);
    float s_kill = lookahead_score(&now, 100, &kill, 250, &why);
    CHECK(strstr(lookahead_reason_text(why), "first kill") != NULL);
    float s_giant = lookahead_score(&now, 100, &giant, 250, &why);
    CHECK(strstr(lookahead_reason_text(why), "giant") != NULL);
    float s_hunter = lookahead_score(&now, 100, &hunter, 250, &why);
    CHECK(strstr(lookahead_reason_text(why), "hunter") != NULL);
    CHECK(s_none < s_child);
    CHECK(s_child < s_giant);
    CHECK(s_giant < s_kill);
    CHECK(s_kill < s_hunter);
    /* A second kill is no first: the line says so. */
    LifeTally one_kill = kill, two_kills = kill;
    two_kills.kills = 2;
    (void)lookahead_score(&one_kill, 100, &two_kills, 250, &why);
    CHECK(why == LOOK_MORE_KILLS);
}

/* The newer reasons and the kinds: a heavy wound and kin lost are named, kin
 * lost counts at most three, and every reason has a kind, the quiet season
 * alone the quiet kind. */
static void test_wounds_kin_and_kinds(void)
{
    LifeTally now = {0};
    LifeTally wound = now, three = now, ten = now;
    wound.heavy_wounds = 1;
    three.kin_lost = 3;
    ten.kin_lost = 10;
    int why = -1;
    (void)lookahead_score(&now, 100, &wound, 250, &why);
    CHECK(why == LOOK_WOUND);
    CHECK(lookahead_kind(why) == LOOK_KIND_FIGHT);
    float s_three = lookahead_score(&now, 100, &three, 250, &why);
    CHECK(why == LOOK_KIN_LOST);
    CHECK(lookahead_kind(why) == LOOK_KIND_FAMILY);
    float s_ten = lookahead_score(&now, 100, &ten, 250, &why);
    CHECK(s_ten == s_three);
    for (int r = 0; r < LOOK_REASON_COUNT; r++) {
        int k = lookahead_kind(r);
        CHECK(k >= 0 && k < LOOK_KIND_COUNT);
        CHECK((k == LOOK_KIND_QUIET) == (r == LOOK_QUIET));
        CHECK(strlen(lookahead_reason_text(r)) > 0);
    }
}

/* Tired of the first pick's kind, the camera picks another kind when one is
 * worth enough, and never names a reason of the tired kind unless it has to:
 * then it is the same creature as the untired pick. */
static void test_tired_kind_turns(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 128, 72));
    for (int t = 0; t < 1200; t++) {
        sim_step(&s);
    }
    Lookahead l;
    lookahead_init(&l, LEAD);
    lookahead_sync_all(&l, &s);
    char why[LOOKAHEAD_WHY];
    int first, second;
    uint32_t a = lookahead_pick(&l, &s, 0, &first, why, sizeof(why));
    CHECK(a != LOOKAHEAD_NOBODY);
    unsigned tired = 1u << lookahead_kind(first);
    uint32_t b = lookahead_pick(&l, &s, tired, &second, why, sizeof(why));
    CHECK(b != LOOKAHEAD_NOBODY);
    CHECK(lookahead_kind(second) != lookahead_kind(first) || b == a);
    lookahead_free(&l);
    sim_free(&s);
}

/* A new world (another seed) or one put back in time is not the copy's past:
 * the copy is dropped and made again. */
static void test_new_world_drops_copy(void)
{
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 3, 64, 32));
    Lookahead l;
    lookahead_init(&l, LEAD);
    lookahead_sync_all(&l, &s);
    CHECK(lookahead_ready(&l, &s));
    CHECK(sim_reseed(&s, 77));
    CHECK(!lookahead_ready(&l, &s));
    lookahead_sync_all(&l, &s);
    CHECK(lookahead_ready(&l, &s));
    CHECK(l.ahead.seed == 77);
    lookahead_free(&l);
    lookahead_free(&l); /* twice is safe */
    sim_free(&s);
}

int main(void)
{
    test_watching_is_free();
    test_copy_is_the_future();
    test_pick_is_alive();
    test_action_makes_a_new_copy();
    test_score_order();
    test_wounds_kin_and_kinds();
    test_tired_kind_turns();
    test_new_world_drops_copy();
    TEST_SUMMARY();
}
