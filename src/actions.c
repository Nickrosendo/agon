#include "actions.h"

#include <stdlib.h> /* realloc, free */

#include "genome.h"
#include "sim.h"

bool actions_add(ActionLog *log, PlayerAction a)
{
    if (log->count > 0 && a.tick < log->items[log->count - 1].tick) {
        return false;
    }
    if (log->count == log->capacity) {
        /* Doubling keeps the copying cheap overall: n entries cost fewer than
         * 2n copies in all. The first time capacity is 0, so start at 16. */
        int bigger = log->capacity == 0 ? 16 : log->capacity * 2;
        /* realloc into a new pointer first: on failure it returns NULL and
         * leaves the old block as it was, and writing NULL straight into
         * log->items would lose the only pointer to that block (a leak). */
        PlayerAction *grown =
            realloc(log->items, (size_t)bigger * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        log->items = grown;
        log->capacity = bigger;
    }
    log->items[log->count] = a; /* a struct assignment copies every field */
    log->count++;
    return true;
}

void actions_clear(ActionLog *log)
{
    log->count = 0;
    log->next = 0;
}

void actions_free(ActionLog *log)
{
    free(log->items);
    *log = (ActionLog){0};
}

void actions_seek(ActionLog *log, uint64_t tick)
{
    log->next = 0;
    while (log->next < log->count && log->items[log->next].tick < tick) {
        log->next++;
    }
}

/* The slot of the living creature with this id, or -1. A walk over the whole
 * pool: actions are rare, so a faster index would not pay for itself. */
static int find_living(const Pool *p, uint32_t id)
{
    for (int i = 0; i < p->capacity; i++) {
        if (p->items[i].alive && p->items[i].id == id) {
            return i;
        }
    }
    return -1;
}

static void apply_trait(Sim *s, const PlayerAction *a)
{
    int idx = find_living(&s->pool, a->creature);
    if (idx < 0 || a->gene >= GENE_TRAITS) {
        return;
    }
    Creature *c = &s->pool.items[idx];
    c->genome.g[a->gene] = a->value;
    /* A smaller or less armoured body has a lower most health: keep the
     * health it has within the new limit, as a newborn's is. */
    float most = creature_max_health(c, &s->set);
    if (c->health > most) {
        c->health = most;
    }
    /* The kinship cache assumes a genome never changes after birth, so what
     * it remembers about this creature is now wrong. Forgetting all of it
     * costs some speed for a moment and changes no result. */
    pool_forget_kinship(&s->pool);
}

void actions_apply_due(Sim *s)
{
    ActionLog *log = &s->actions;
    while (log->next < log->count && log->items[log->next].tick <= s->tick) {
        const PlayerAction *a = &log->items[log->next];
        /* An entry for a tick already gone (only a damaged log has one) is
         * skipped rather than applied late. */
        if (a->tick == s->tick && a->kind == ACTION_TRAIT) {
            apply_trait(s, a);
        }
        log->next++;
    }
}
