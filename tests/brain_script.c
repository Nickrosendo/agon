#include "brain_script.h"

#include <stdlib.h> /* calloc, free */

typedef struct {
    Action actions[SCRIPT_MAX];
    int count, next;
    int decisions, births, deaths;
    Senses last_senses;
} Script;

static bool script_create(Brain *b, const Settings *s, uint64_t seed)
{
    (void)s;
    (void)seed;
    b->state = calloc(1, sizeof(Script)); /* an empty script */
    return b->state != NULL;
}

static void script_destroy(Brain *b)
{
    free(b->state);
    b->state = NULL;
}

static bool script_decide(Brain *b, uint32_t id, const Genome *g, Mind *mind,
                          const Senses *in, Action *out)
{
    (void)id;
    (void)g;
    (void)mind;
    Script *sc = b->state;
    sc->decisions++;
    sc->last_senses = *in;
    if (sc->next >= sc->count) {
        return false;
    }
    *out = sc->actions[sc->next];
    sc->next++;
    return true;
}

static void script_on_birth(Brain *b, uint32_t id, const Genome *g)
{
    (void)id;
    (void)g;
    Script *sc = b->state;
    sc->births++;
}

static void script_learn(Brain *b, Mind *mind, float reward, float rate,
                         const Settings *s)
{
    (void)b;
    (void)mind;
    (void)reward;
    (void)rate;
    (void)s;
}

static void script_on_death(Brain *b, uint32_t id)
{
    (void)id;
    Script *sc = b->state;
    sc->deaths++;
}

Senses brain_script_last_senses(const Brain *b)
{
    const Script *sc = b->state;
    return sc->last_senses;
}

void brain_script_load(Brain *b, const Action *actions, int count)
{
    Script *sc = b->state;
    if (count > SCRIPT_MAX) {
        count = SCRIPT_MAX;
    }
    for (int i = 0; i < count; i++) {
        sc->actions[i] = actions[i];
    }
    sc->count = count;
    sc->next = 0;
}

/* A const Brain still has a non-const state pointer: const on the struct
 * makes the pointer itself read-only, not what it points to. */
int brain_script_decisions(const Brain *b)
{
    const Script *sc = b->state;
    return sc->decisions;
}

int brain_script_births(const Brain *b)
{
    const Script *sc = b->state;
    return sc->births;
}

int brain_script_deaths(const Brain *b)
{
    const Script *sc = b->state;
    return sc->deaths;
}

const Brain BRAIN_SCRIPT = {
    .name = "script",
    .create = script_create,
    .destroy = script_destroy,
    .decide = script_decide,
    .learn = script_learn,
    .on_birth = script_on_birth,
    .on_death = script_on_death,
};
