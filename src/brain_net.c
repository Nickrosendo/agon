/*
 * The neural-network brain: each creature's genome holds the weights of a
 * small network that turns its senses into an action. The brain itself
 * keeps nothing: everything it needs arrives with each call, so it has no
 * state to create or destroy. Nothing here rewards a good network; better
 * networks become common only because their owners live long enough to
 * split more often.
 */
#include <assert.h> /* static_assert */
#include <math.h>   /* tanhf */
#include <stddef.h> /* NULL: a macro from a header, not a keyword */

#include "brain.h"
#include "genome.h"

/* Checked by the compiler, not when the program runs: if either is false
 * the build stops with the message. The network has one input per sense
 * and one output per action, and the two headers count them separately. */
static_assert(NET_IN == SENSE_COUNT, "one network input per sense");
static_assert(NET_OUT == ACT_COUNT, "one network output per action");

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

/* The output layer: a weighted sum of the hidden values plus a bias, with
 * nothing squashing it. Returns the action whose output is largest; on a
 * tie, the first of them (ACT_REST when every output is 0). */
static uint8_t strongest_output(const Genome *g, const float hidden[NET_HIDDEN])
{
    int best = 0;
    float best_sum = 0.0f;
    for (int o = 0; o < NET_OUT; o++) {
        float sum = g->g[net_out_weight(o, NET_HIDDEN)];
        for (int j = 0; j < NET_HIDDEN; j++) {
            sum += g->g[net_out_weight(o, j)] * hidden[j];
        }
        if (o == 0 || sum > best_sum) {
            best = o;
            best_sum = sum;
        }
    }
    return (uint8_t)best;
}

static bool net_decide(Brain *b, uint32_t id, const Genome *g,
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
    out->kind = strongest_output(g, hidden);
    out->hold = 1; /* decide again next tick: the senses may change */
    return true;
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
    .on_birth = net_on_birth,
    .on_death = net_on_death,
};
