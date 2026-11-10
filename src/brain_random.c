/*
 * The random walker: ignores its senses and picks any action, held for a
 * few ticks. The first brain behind the interface, before genomes exist.
 */
#include <stdlib.h> /* malloc, free */

#include "brain.h"
#include "rng.h"

/* The longest hold the walker asks for, in ticks. */
#define RANDOM_HOLD_MAX 3

/* The walker's state is just its own generator, on the heap, so two
 * walkers opened at once each have their own. */
static bool random_create(Brain *b, const Settings *s, uint64_t seed)
{
    (void)s; /* unused; the cast to void silences the warning */
    Rng *r = malloc(sizeof(*r));
    if (r == NULL) {
        return false;
    }
    rng_seed(r, seed);
    b->state = r; /* any object pointer converts to void * by itself */
    return true;
}

static void random_destroy(Brain *b)
{
    free(b->state);
    b->state = NULL;
}

static bool random_decide(Brain *b, uint32_t id, const Genome *g, Mind *mind,
                          const Senses *in, Action *out)
{
    (void)id;
    (void)g;
    (void)mind;
    (void)in;
    /* Back from void * to what create stored. C converts without a cast
     * and cannot check it: had state pointed to something else, this
     * line would compile and the program would read garbage. */
    Rng *r = b->state;
    out->kind = (uint8_t)rng_below(r, ACT_COUNT);
    out->hold = (uint16_t)(1 + rng_below(r, RANDOM_HOLD_MAX));
    return true;
}

static void random_on_birth(Brain *b, uint32_t id, const Genome *g)
{
    (void)b;
    (void)id;
    (void)g;
}

/* A random brain learns nothing. */
static void random_learn(Brain *b, Mind *mind, float reward, float rate,
                         const Settings *s)
{
    (void)b;
    (void)mind;
    (void)reward;
    (void)rate;
    (void)s;
}

static void random_on_death(Brain *b, uint32_t id)
{
    (void)b;
    (void)id;
}

/* The template. A function's name on its own, without (), is its
 * address, which is what a function pointer field holds. */
const Brain BRAIN_RANDOM = {
    .name = "random",
    .state = NULL,
    .create = random_create,
    .destroy = random_destroy,
    .decide = random_decide,
    .learn = random_learn,
    .on_birth = random_on_birth,
    .on_death = random_on_death,
};
