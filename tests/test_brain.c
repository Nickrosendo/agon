#include "test.h"

#include <math.h> /* NAN, INFINITY, isfinite, fabsf */

#include "brain.h"
#include "brain_script.h"
#include "genome.h"
#include "rng.h"
#include "settings.h"

static void test_open_by_name(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "random", &set, 1));
    CHECK(b.state != NULL);
    brain_close(&b);
    CHECK(b.state == NULL);
    brain_close(&b); /* a second close is harmless */

    CHECK(!brain_open(&b, "nope", &set, 1));
    CHECK(b.decide == NULL);
    brain_close(&b); /* so is closing a brain that never opened */
}

static void test_random_always_ready(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "random", &set, 1));
    Senses in = {0};
    int bad = 0;
    for (int i = 0; i < 1000; i++) {
        Action a = {0};
        if (!b.decide(&b, 1, NULL, NULL, &in, &a) || a.kind >= ACT_COUNT ||
            a.hold < 1) {
            bad++;
        }
    }
    CHECK(bad == 0);
    brain_close(&b);
}

static void test_random_same_seed_same_actions(void)
{
    Settings set = settings_default();
    Brain a, b;
    CHECK(brain_open(&a, "random", &set, 42));
    CHECK(brain_open(&b, "random", &set, 42));
    Senses in = {0};
    int differ = 0;
    int kinds_seen[ACT_COUNT] = {0};
    /* The two calls alternate, so this also shows the two brains do not
     * share a generator. */
    for (int i = 0; i < 1000; i++) {
        Action x, y;
        a.decide(&a, 1, NULL, NULL, &in, &x);
        b.decide(&b, 1, NULL, NULL, &in, &y);
        if (x.kind != y.kind || x.hold != y.hold) {
            differ++;
        }
        kinds_seen[x.kind]++;
    }
    CHECK(differ == 0);
    for (int k = 0; k < ACT_COUNT; k++) {
        CHECK(kinds_seen[k] > 0);
    }
    brain_close(&a);
    brain_close(&b);
}

static void test_script_plays_back(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "script", &set, 0));
    Action script[] = {{.kind = ACT_MOVE, .hold = 3}, {.kind = ACT_BITE, .hold = 1}, {.kind = ACT_TURN_LEFT, .hold = 2}};
    brain_script_load(&b, script, 3);
    Senses in = {0};
    Action out;
    CHECK(b.decide(&b, 1, NULL, NULL, &in, &out) && out.kind == ACT_MOVE &&
          out.hold == 3);
    CHECK(b.decide(&b, 1, NULL, NULL, &in, &out) && out.kind == ACT_BITE);
    CHECK(b.decide(&b, 1, NULL, NULL, &in, &out) && out.kind == ACT_TURN_LEFT);
    CHECK(!b.decide(&b, 1, NULL, NULL, &in, &out));
    CHECK(!b.decide(&b, 1, NULL, NULL, &in, &out));
    CHECK(brain_script_decisions(&b) == 5);
    brain_close(&b);
}

/* A genome with every value 0 but the sharpness gene: no weight, so every
 * output of the network is 0. Built as a compound literal, which zeroes it all.
 * The sharpness gene at its top makes the choice all but always the strongest
 * output, which the tests below that expect one action rely on. */
static Genome zero_genome(void)
{
    Genome g = (Genome){0};
    g.g[GENE_SURE] = 1.0f;
    return g;
}

/* The sum of the chances, and whether each is a number in 0..1. */
static bool chances_valid(const float chances[ACT_COUNT])
{
    float sum = 0.0f;
    for (int o = 0; o < ACT_COUNT; o++) {
        if (!(chances[o] >= 0.0f && chances[o] <= 1.0f)) {
            return false; /* also false for NaN */
        }
        sum += chances[o];
    }
    return fabsf(sum - 1.0f) < 1e-4f;
}

static void test_chances_sum_to_one(void)
{
    Genome g;
    Senses in;
    Settings set = settings_default();
    Rng r;
    rng_seed(&r, 3);
    for (int k = 0; k < 100; k++) {
        genome_random(&g, &r, &set);
        for (int i = 0; i < SENSE_COUNT; i++) {
            in.v[i] = rng_unit(&r);
        }
        float chances[ACT_COUNT];
        brain_net_chances(&g, NULL, &in, chances);
        CHECK(chances_valid(chances));
    }
}

static void test_chances_follow_outputs(void)
{
    /* Middling sharpness: every action keeps some chance, but the larger
     * output always has the larger chance. */
    Genome g = zero_genome();
    g.g[GENE_SURE] = 0.2f;
    g.g[net_out_weight(ACT_MOVE, NET_HIDDEN)] = 1.0f;
    g.g[net_out_weight(ACT_BITE, NET_HIDDEN)] = 0.5f;
    Senses in = {0};
    float chances[ACT_COUNT];
    brain_net_chances(&g, NULL, &in, chances);
    CHECK(chances_valid(chances));
    CHECK(chances[ACT_MOVE] > chances[ACT_BITE]);
    CHECK(chances[ACT_BITE] > chances[ACT_REST]);
    CHECK(chances[ACT_REST] > 0.0f); /* not never */
    CHECK(chances[ACT_REST] == chances[ACT_SLEEP]); /* equal outputs, equal chances */
    /* Blunter still: the gap between move and rest narrows. */
    float sharp_gap = chances[ACT_MOVE] - chances[ACT_REST];
    g.g[GENE_SURE] = 0.0f;
    brain_net_chances(&g, NULL, &in, chances);
    CHECK(chances[ACT_MOVE] - chances[ACT_REST] < sharp_gap);
}

static void test_sharp_end_is_strongest(void)
{
    Genome g = zero_genome(); /* sharpness gene at 1 */
    g.g[net_out_weight(ACT_MOVE, NET_HIDDEN)] = 1.0f;
    Senses in = {0};
    float chances[ACT_COUNT];
    brain_net_chances(&g, NULL, &in, chances);
    CHECK(chances[ACT_MOVE] > 0.99f);
}

static void test_softmax_survives_huge_outputs(void)
{
    /* Outputs of about plus and minus a thousand: exp of them overflows a
     * float unless the largest is taken off first. */
    Genome g = zero_genome();
    g.g[net_out_weight(ACT_MOVE, NET_HIDDEN)] = 1000.0f;
    g.g[net_out_weight(ACT_BITE, NET_HIDDEN)] = 999.0f;
    g.g[net_out_weight(ACT_REST, NET_HIDDEN)] = -1000.0f;
    Senses in = {0};
    float chances[ACT_COUNT];
    brain_net_chances(&g, NULL, &in, chances);
    CHECK(chances_valid(chances));
    CHECK(chances[ACT_MOVE] > 0.99f);
}

static void test_draw_picks_action(void)
{
    /* Every output equal: each action has the same chance, and the draw alone
     * decides. A draw of 0 falls in the first action's share, a draw just
     * below 1 in the last one's. */
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Genome g = zero_genome();
    Senses in = {0};
    Action a = {0};
    in.draw = 0.0f;
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a) && a.kind == 0);
    in.draw = 0.999f;
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a) && a.kind == ACT_COUNT - 1);
    in.draw = 3.5f / (float)ACT_COUNT; /* inside the fourth share */
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a) && a.kind == 3);
    brain_close(&b);
}

static void test_net_bias_decides(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Genome g = zero_genome();
    /* Only the move output's bias is set: it wins whatever the senses. */
    g.g[net_out_weight(ACT_MOVE, NET_HIDDEN)] = 3.0f;
    Rng r;
    rng_seed(&r, 9);
    int not_move = 0;
    for (int k = 0; k < 1000; k++) {
        Senses in;
        for (int i = 0; i < SENSE_COUNT; i++) {
            in.v[i] = rng_unit(&r);
        }
        in.draw = rng_unit(&r);
        Action a = {0};
        if (!b.decide(&b, 1, &g, NULL, &in, &a) || a.kind != ACT_MOVE ||
            a.hold < 1) {
            not_move++;
        }
    }
    CHECK(not_move == 0);
    brain_close(&b);
}

static void test_net_sees_vegetation(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    /* One path only: the vegetation ahead into hidden value 0, and hidden
     * value 0 into the bite output. */
    Genome g = zero_genome();
    int veg_ahead = SENSE_RAYS + RAY_AHEAD * RAY_VALUES + RAY_VEG;
    g.g[net_hidden_weight(0, veg_ahead)] = 2.0f;
    g.g[net_out_weight(ACT_BITE, 0)] = 2.0f;

    /* A draw away from the ends: a draw of 0 falls in the first action's share
     * however tiny that share is. With every output equal (no vegetation) a
     * draw of 0.3 falls in the third action's share, a turn. */
    Senses in = {.draw = 0.3f};
    Action a = {0};
    in.v[veg_ahead] = 1.0f;
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a) && a.kind == ACT_BITE);
    in.v[veg_ahead] = 0.0f;
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a) && a.kind != ACT_BITE);
    brain_close(&b);
}

static void test_memory_changes_a_decision(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    /* One path: the first memory value into hidden value 0, and that into bite. */
    Genome g = zero_genome();
    g.g[net_hidden_weight(0, SENSE_MEMORY)] = 2.0f;
    g.g[net_out_weight(ACT_BITE, 0)] = 2.0f;
    /* The same world senses, all zero; only what it remembers differs. */
    Senses in = {.draw = 0.5f};
    Action a = {0};
    in.v[SENSE_MEMORY] = 1.0f;
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a) && a.kind == ACT_BITE);
    in.v[SENSE_MEMORY] = -1.0f;
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a) && a.kind != ACT_BITE);
    brain_close(&b);
}

static void test_net_writes_memory_and_it_never_chooses(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Genome g = zero_genome();
    /* A huge output for the second memory value. Were the memory outputs in the
     * race for the strongest, this would pick an action past the last. */
    g.g[net_out_weight(ACT_COUNT + 1, NET_HIDDEN)] = 4.0f;
    Senses in = {0};
    Action a = {0};
    CHECK(b.decide(&b, 1, &g, NULL, &in, &a));
    CHECK(a.kind == ACT_REST);
    CHECK(a.memory[0] == 0.0f);
    CHECK(a.memory[1] > 0.99f && a.memory[1] <= 1.0f); /* through tanh */
    CHECK(a.memory[2] == 0.0f);
    brain_close(&b);
}

static void test_net_needs_a_genome(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Senses in = {0};
    Action a = {0};
    CHECK(!b.decide(&b, 1, NULL, NULL, &in, &a)); /* no genome: not ready */
    brain_close(&b);
}

/* A random genome and random senses, for the learning tests. */
static void learning_setup(Genome *g, Senses *in, uint64_t seed)
{
    Settings set = settings_default();
    Rng r;
    rng_seed(&r, seed);
    genome_random(g, &r, &set);
    for (int i = 0; i < SENSE_COUNT; i++) {
        in->v[i] = rng_unit(&r);
    }
    in->draw = rng_unit(&r);
}

static void test_learns_from_a_meal(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Genome g;
    Senses in;
    learning_setup(&g, &in, 31);
    Mind mind = {0};
    Action a = {0};
    CHECK(b.decide(&b, 1, &g, &mind, &in, &a));
    uint8_t chosen = a.kind;
    b.learn(&b, &mind, 1.0f, 0.05f, &set);
    /* The chosen action's learned bias rose; every other action's row is 0. */
    CHECK(mind.adjust[chosen * (NET_HIDDEN + 1) + NET_HIDDEN] > 0.0f);
    for (int o = 0; o < ACT_COUNT; o++) {
        for (int j = 0; j <= NET_HIDDEN && o != chosen; j++) {
            CHECK(mind.adjust[o * (NET_HIDDEN + 1) + j] == 0.0f);
        }
    }
    /* Rewarded again and again, it keeps choosing it. */
    for (int k = 0; k < 20; k++) {
        CHECK(b.decide(&b, 1, &g, &mind, &in, &a) && a.kind == chosen);
        b.learn(&b, &mind, 1.0f, 0.05f, &set);
    }
    brain_close(&b);
}

static void test_pain_unlearns(void)
{
    Settings set = settings_default();
    set.learn_limit = 1000.0f; /* room enough to turn any choice */
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Genome g;
    Senses in;
    learning_setup(&g, &in, 32);
    Mind mind = {0};
    Action a = {0};
    CHECK(b.decide(&b, 1, &g, &mind, &in, &a));
    uint8_t first = a.kind;
    for (int k = 0; k < 500 && a.kind == first; k++) {
        b.learn(&b, &mind, -1.0f, 0.05f, &set);
        CHECK(b.decide(&b, 1, &g, &mind, &in, &a));
    }
    CHECK(a.kind != first); /* it hurt every time: it chose something else */
    brain_close(&b);
}

static void test_no_rate_no_learning(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Genome g;
    Senses in;
    learning_setup(&g, &in, 33);
    Mind mind = {0};
    Action a = {0};
    CHECK(b.decide(&b, 1, &g, &mind, &in, &a));
    b.learn(&b, &mind, 5.0f, 0.0f, &set);
    for (int i = 0; i < MIND_LEN; i++) {
        CHECK(mind.adjust[i] == 0.0f);
    }
    brain_close(&b);
}

static void test_learning_stays_finite(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Genome g;
    Senses in;
    learning_setup(&g, &in, 34);
    Mind mind = {0};
    Action a = {0};
    float rewards[] = {1e30f, NAN, INFINITY, -INFINITY, -1e30f};
    for (int k = 0; k < 5; k++) {
        CHECK(b.decide(&b, 1, &g, &mind, &in, &a));
        b.learn(&b, &mind, rewards[k], 0.05f, &set);
        for (int i = 0; i < MIND_LEN; i++) {
            CHECK(isfinite(mind.adjust[i]));
            CHECK(fabsf(mind.adjust[i]) <= set.learn_limit);
        }
    }
    brain_close(&b);
}

int main(void)
{
    /* The scripted brain is not built in; registering it is the only
     * step needed before brain_open can find it. */
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_open_by_name();
    test_random_always_ready();
    test_random_same_seed_same_actions();
    test_script_plays_back();
    test_net_bias_decides();
    test_learns_from_a_meal();
    test_pain_unlearns();
    test_no_rate_no_learning();
    test_learning_stays_finite();
    test_net_sees_vegetation();
    test_memory_changes_a_decision();
    test_net_writes_memory_and_it_never_chooses();
    test_net_needs_a_genome();
    test_chances_sum_to_one();
    test_chances_follow_outputs();
    test_sharp_end_is_strongest();
    test_softmax_survives_huge_outputs();
    test_draw_picks_action();
    TEST_SUMMARY();
}
