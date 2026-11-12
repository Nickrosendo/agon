/*
 * The neural-network brain: each creature's genome holds the weights of a
 * small network that turns its senses into an action. The brain itself
 * keeps nothing: everything it needs arrives with each call, so it has no
 * state to create or destroy. Its choice has chance in it: each action is
 * chosen with a chance that grows with its output (brain_net_chances, brain.h),
 * using the random number the Sim puts in Senses.draw. Nothing here rewards a good network; better
 * networks become common only because their owners live long enough to
 * have more children.
 */
#include <assert.h> /* static_assert */
#include <math.h>   /* tanhf, expf, isfinite */
#include <stddef.h> /* NULL: a macro from a header, not a keyword */

#include "brain.h"
#include "genome.h"

/* Checked by the compiler, not when the program runs: if either is false
 * the build stops with the message. The network has one input per sense
 * and one output per action, and the two headers count them separately. */
static_assert(NET_IN == SENSE_COUNT, "one network input per sense");
static_assert(NET_HIDDEN == MIND_HIDDEN, "a Mind holds one weight per hidden value");
static_assert(NET_OUT == ACT_COUNT + MEMORY_VALUES + 1,
              "one network output per action and per memory value, and the call");

static bool net_create(Brain *b, const Settings *s, uint64_t seed)
{
    (void)s;
    (void)seed;
    b->state = NULL; /* nothing to keep */
    return true;
}

static void net_destroy(Brain *b)
{
    b->state = NULL;
}

/* The hidden layer: each hidden value is tanh of a weighted sum of the
 * senses plus a bias. tanh squashes any number into -1..1. */
static void hidden_layer(const Genome *g, const Senses *in,
                         float hidden[NET_HIDDEN])
{
    for (int h = 0; h < NET_HIDDEN; h++) {
        float sum = g->g[net_hidden_weight(h, NET_IN)]; /* the bias */
        for (int i = 0; i < NET_IN; i++) {
            sum += g->g[net_hidden_weight(h, i)] * in->v[i];
        }
        hidden[h] = tanhf(sum);
    }
}

/* One output: a weighted sum of the hidden values plus a bias, with nothing
 * squashing it. */
static float output(const Genome *g, const float hidden[NET_HIDDEN], int o)
{
    float sum = g->g[net_out_weight(o, NET_HIDDEN)];
    for (int j = 0; j < NET_HIDDEN; j++) {
        sum += g->g[net_out_weight(o, j)] * hidden[j];
    }
    return sum;
}

/* What the mind adds to action o's output: its learned weights times the
 * hidden values, plus its learned bias. */
static float learned(const Mind *mind, const float hidden[NET_HIDDEN], int o)
{
    const float *row = &mind->adjust[o * (NET_HIDDEN + 1)];
    float sum = row[NET_HIDDEN];
    for (int j = 0; j < NET_HIDDEN; j++) {
        sum += row[j] * hidden[j];
    }
    return sum;
}

/* How sharp the choice is, from the sure gene: SURE_MIN at 0, SURE_MAX at 1. */
static float sharpness(const Genome *g)
{
    return SURE_MIN + g->g[GENE_SURE] * (SURE_MAX - SURE_MIN);
}

/* Each action's chance, from the hidden values: a softmax. The memory outputs,
 * which come after the actions', have no say in this.
 *
 * exp(x) for x above about 88 is larger than any float, and becomes infinity;
 * infinity divided by infinity is NaN. Taking the largest output off every
 * output first changes no chance (it divides top and bottom by the same
 * exp(largest)), and leaves every exponent at 0 or below, so each exp is in
 * 0..1 and the largest is exactly 1: the total is never 0. */
static void chances_from_hidden(const Genome *g, const Mind *mind,
                                const float hidden[NET_HIDDEN],
                                float chances[ACT_COUNT])
{
    float outs[ACT_COUNT];
    float largest = 0.0f;
    for (int o = 0; o < ACT_COUNT; o++) {
        outs[o] = output(g, hidden, o);
        if (mind != NULL) {
            outs[o] += learned(mind, hidden, o);
        }
        if (o == 0 || outs[o] > largest) {
            largest = outs[o];
        }
    }
    float sharp = sharpness(g);
    float total = 0.0f;
    for (int o = 0; o < ACT_COUNT; o++) {
        chances[o] = expf((outs[o] - largest) * sharp);
        total += chances[o];
    }
    for (int o = 0; o < ACT_COUNT; o++) {
        chances[o] /= total;
    }
}

/* The action whose share of 0..1 the draw falls in, the shares laid end to end
 * in ACT_ order. A draw at or past the last edge (rounding can leave the sum of
 * the chances a hair below 1) gives the last action with any chance. */
static uint8_t drawn_action(const float chances[ACT_COUNT], float draw)
{
    float edge = 0.0f;
    int last = 0;
    for (int o = 0; o < ACT_COUNT; o++) {
        if (chances[o] <= 0.0f) {
            continue;
        }
        edge += chances[o];
        last = o;
        if (draw < edge) {
            return (uint8_t)o;
        }
    }
    return (uint8_t)last;
}

void brain_net_chances(const Genome *g, const Mind *mind, const Senses *in,
                       float chances[ACT_COUNT])
{
    float hidden[NET_HIDDEN];
    hidden_layer(g, in, hidden);
    chances_from_hidden(g, mind, hidden, chances);
}

/* Marks in the trace which hidden values were active when action o was
 * chosen, so a reward soon after can be credited to them. */
static void mark(Mind *mind, const float hidden[NET_HIDDEN], int o)
{
    float *row = &mind->trace[o * (NET_HIDDEN + 1)];
    for (int j = 0; j < NET_HIDDEN; j++) {
        row[j] += hidden[j];
    }
    row[NET_HIDDEN] += 1.0f;
}

static bool net_decide(Brain *b, uint32_t id, const Genome *g, Mind *mind,
                       const Senses *in, Action *out)
{
    (void)b;
    (void)id;
    if (g == NULL) {
        return false; /* a creature without a genome cannot think */
    }
    /* A local array: on the stack, NET_HIDDEN floats, gone when the
     * function returns. Passed below as a pointer to its first element. */
    float hidden[NET_HIDDEN];
    hidden_layer(g, in, hidden);
    float chances[ACT_COUNT];
    chances_from_hidden(g, mind, hidden, chances);
    out->kind = drawn_action(chances, in->draw);
    if (mind != NULL) {
        mark(mind, hidden, out->kind);
    }
    out->hold = 1; /* decide again next tick: the senses may change */
    for (int m = 0; m < MEMORY_VALUES; m++) {
        out->memory[m] = tanhf(output(g, hidden, ACT_COUNT + m));
    }
    out->call = tanhf(output(g, hidden, ACT_COUNT + MEMORY_VALUES));
    return true;
}

/* adjust += rate * reward * trace, kept within plus or minus learn_limit; then
 * the trace fades. A reward that is not a number (NaN or infinity, from a
 * settings file with absurd values, say) teaches nothing: one NaN added to an
 * adjustment would make it NaN for ever, and every comparison with NaN is false,
 * so that action would never again be chosen or never stop being chosen. */
static void net_learn(Brain *b, Mind *mind, float reward, float rate,
                      const Settings *s)
{
    (void)b;
    float step = rate * reward;
    if (!isfinite(step)) {
        step = 0.0f;
    }
    float limit = s->learn_limit;
    for (int i = 0; i < MIND_LEN; i++) {
        float v = mind->adjust[i] + step * mind->trace[i];
        /* !(v >= -limit) is also true for NaN, which then becomes -limit. */
        if (!(v >= -limit)) {
            v = -limit;
        } else if (v > limit) {
            v = limit;
        }
        mind->adjust[i] = v;
        mind->trace[i] *= s->learn_trace_fade;
    }
}

static void net_on_birth(Brain *b, uint32_t id, const Genome *g)
{
    (void)b;
    (void)id;
    (void)g;
}

static void net_on_death(Brain *b, uint32_t id)
{
    (void)b;
    (void)id;
}

const Brain BRAIN_NET = {
    .name = "net",
    .state = NULL,
    .create = net_create,
    .destroy = net_destroy,
    .decide = net_decide,
    .learn = net_learn,
    .on_birth = net_on_birth,
    .on_death = net_on_death,
};
