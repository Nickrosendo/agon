/*
 * Slow tests: too long for every "make test", run by "make test-slow".
 * Built like the other tests, checkers included, which is most of why
 * they are slow: AddressSanitizer checks every memory access.
 */
#include "test.h"

#include <math.h>    /* isfinite */
#include <stdbool.h>

#include "settings.h"
#include "sim.h"

/* True when the creature's energy, health, body and every value of its genome is an
 * ordinary number: not infinity and not NaN. */
static bool creature_finite(const Creature *c, const Settings *set)
{
    if (!isfinite(c->energy) || !isfinite(c->health) || !isfinite(c->grown) ||
        !isfinite(creature_size(c, set))) {
        return false;
    }
    if (!isfinite(creature_upkeep_cost(c, set))) {
        return false;
    }
    for (int i = 0; i < GENOME_LEN; i++) {
        if (!isfinite(c->genome.g[i])) {
            return false;
        }
    }
    return true;
}

static void test_long_run_stays_finite(void)
{
    /* 60,000 ticks at full size is many generations, on a seed measured to last:
     * a small world now dies out, and a test with nobody left proves nothing.
     * Lives are ended by what ageing costs and not by a fixed limit. Mutations
     * add up over that many generations, and a NaN, once in a genome, would be
     * inherited by every descendant. Very old creatures are where an ageing
     * cost could run away, so ages are checked through their upkeep too. */
    Settings set = settings_default();
    Sim s;
    CHECK(sim_init(&s, &set, 1, 256, 144));
    for (int i = 0; i < 60000; i++) {
        sim_step(&s);
    }
    int bad = 0;
    for (int i = 0; i < s.pool.capacity; i++) {
        if (s.pool.items[i].alive && !creature_finite(&s.pool.items[i], &s.set)) {
            bad++;
        }
    }
    CHECK(bad == 0);
    CHECK(s.pool.count > 0);
    printf("seed 1 after 60000 steps: %d alive, %u born in all\n",
           s.pool.count, (unsigned)s.pool.next_id);
    sim_free(&s);
}

int main(void)
{
    test_long_run_stays_finite();
    TEST_SUMMARY();
}
