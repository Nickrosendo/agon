#include "test.h"

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
        if (!b.decide(&b, 1, NULL, &in, &a) || a.kind >= ACT_COUNT ||
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
        a.decide(&a, 1, NULL, &in, &x);
        b.decide(&b, 1, NULL, &in, &y);
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
    Action script[] = {{ACT_MOVE, 3}, {ACT_BITE, 1}, {ACT_TURN_LEFT, 2}};
    brain_script_load(&b, script, 3);
    Senses in = {0};
    Action out;
    CHECK(b.decide(&b, 1, NULL, &in, &out) && out.kind == ACT_MOVE &&
          out.hold == 3);
    CHECK(b.decide(&b, 1, NULL, &in, &out) && out.kind == ACT_BITE);
    CHECK(b.decide(&b, 1, NULL, &in, &out) && out.kind == ACT_TURN_LEFT);
    CHECK(!b.decide(&b, 1, NULL, &in, &out));
    CHECK(!b.decide(&b, 1, NULL, &in, &out));
    CHECK(brain_script_decisions(&b) == 5);
    brain_close(&b);
}

/* A genome with every value 0: no trait, no weight, so every output of
 * the network is 0. Built as a compound literal, which zeroes it all. */
static Genome zero_genome(void)
{
    return (Genome){0};
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
        Action a = {0};
        if (!b.decide(&b, 1, &g, &in, &a) || a.kind != ACT_MOVE ||
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

    Senses in = {0};
    Action a = {0};
    in.v[veg_ahead] = 1.0f;
    CHECK(b.decide(&b, 1, &g, &in, &a) && a.kind == ACT_BITE);
    in.v[veg_ahead] = 0.0f;
    CHECK(b.decide(&b, 1, &g, &in, &a) && a.kind != ACT_BITE);
    brain_close(&b);
}

static void test_net_needs_a_genome(void)
{
    Settings set = settings_default();
    Brain b;
    CHECK(brain_open(&b, "net", &set, 1));
    Senses in = {0};
    Action a = {0};
    CHECK(!b.decide(&b, 1, NULL, &in, &a)); /* no genome: not ready */
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
    test_net_sees_vegetation();
    test_net_needs_a_genome();
    TEST_SUMMARY();
}
