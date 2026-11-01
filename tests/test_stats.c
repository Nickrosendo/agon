#include "test.h"

#include <stdio.h> /* snprintf */

#include "brain_script.h"
#include "settings.h"
#include "sim.h"
#include "stats.h"

static void test_bands(void)
{
    CHECK(stats_band(2.0f) == BAND_PLANT);
    CHECK(stats_band(2.49f) == BAND_PLANT);
    CHECK(stats_band(2.5f) == BAND_MEAT);
    CHECK(stats_band(3.49f) == BAND_MEAT);
    CHECK(stats_band(3.5f) == BAND_TOP);
    CHECK(stats_band(5.0f) == BAND_TOP);
}

static void test_history_partly_filled(void)
{
    History h = {0};
    history_add(&h, 10);
    history_add(&h, 11);
    history_add(&h, 12);
    CHECK(h.len == 3);
    CHECK(history_at(&h, 0) == 10);
    CHECK(history_at(&h, 2) == 12);
}

static void test_history_keeps_the_newest(void)
{
    /* HISTORY + 5 values, 0, 1, 2, ...: the first five are overwritten,
     * so the oldest left is 5 and the newest HISTORY + 4. */
    History h = {0};
    for (int i = 0; i < HISTORY + 5; i++) {
        history_add(&h, i);
    }
    CHECK(h.len == HISTORY);
    for (int k = 0; k < HISTORY; k++) {
        CHECK(history_at(&h, k) == k + 5);
    }
}

/* A Sim with no creatures, no plants and soil everywhere. */
static void make_empty_sim(Sim *sim)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "script");
    set.start_creatures = 0;
    set.min_creatures = 0;
    set.veg_start_chance = 0.0f;
    CHECK(sim_init(sim, &set, 7, 16, 16));
    for (int i = 0; i < 16 * 16; i++) {
        sim->world.terrain[i] = TERRAIN_SOIL;
    }
}

static void test_sample_by_hand(void)
{
    Sim sim;
    make_empty_sim(&sim);
    sim.world.veg[3] = 1;
    sim.world.veg[4] = VEG_MAX;
    sim.world.veg[5] = 2;
    sim.world.veg_wither[5] = 4;
    sim.world.veg_wither[6] = 9; /* bare: not counted as withering */
    sim.world.carrion[20] = 3.0f;
    sim.world.carrion[21] = 1.5f;
    /* Three creatures: two of one family, one founder of another; one at
     * each band. */
    int a = pool_spawn(&sim.pool, &sim.world, 8, 8);
    int b = pool_spawn(&sim.pool, &sim.world, 9, 8);
    int c = pool_spawn(&sim.pool, &sim.world, 10, 8);
    CHECK(a >= 0 && b >= 0 && c >= 0);
    sim.pool.items[b].lineage = sim.pool.items[a].lineage;
    sim.pool.items[b].level = 2.5f;
    sim.pool.items[c].level = 4.0f;
    sim.pool.items[c].age = 300;
    sim.pool.items[a].age = 12;

    stats_sample(&sim.stats, &sim.world, &sim.pool, 99, sim.scratch);
    const Stats *st = &sim.stats;
    CHECK(st->tick == 99);
    CHECK(st->creatures == 3);
    CHECK(st->lineages == 2);
    CHECK(st->by_level[BAND_PLANT] == 1);
    CHECK(st->by_level[BAND_MEAT] == 1);
    CHECK(st->by_level[BAND_TOP] == 1);
    CHECK(st->oldest_age == 300);
    CHECK(st->planted == 3);
    CHECK(st->veg_total == 1 + VEG_MAX + 2);
    CHECK(st->withering == 1);
    CHECK(st->carrion_cells == 2);
    CHECK(st->carrion_total == 4.5f); /* exact: both are halves */
    /* The graphs: plant-eaters, meat-eaters (both higher bands), plants. */
    int last = st->hist_planted.len - 1;
    CHECK(history_at(&st->hist_plant_eaters, last) == 1);
    CHECK(history_at(&st->hist_meat_eaters, last) == 2);
    CHECK(history_at(&st->hist_planted, last) == 3);
    sim_free(&sim);
}

/* How many different lineages the living creatures have, the slow way:
 * a creature counts if no living creature before it shares its lineage. */
static int distinct_lineages(const Pool *p)
{
    int n = 0;
    for (int i = 0; i < p->capacity; i++) {
        if (!p->items[i].alive) {
            continue;
        }
        int seen = 0;
        for (int j = 0; j < i && !seen; j++) {
            seen = p->items[j].alive &&
                   p->items[j].lineage == p->items[i].lineage;
        }
        if (!seen) {
            n++;
        }
    }
    return n;
}

static void test_sampled_while_running(void)
{
    Settings set = settings_default();
    set.stats_every = 1;
    Sim sim;
    CHECK(sim_init(&sim, &set, 7, 64, 32));
    /* Sampled once by sim_init, at tick 0. */
    CHECK(sim.stats.hist_planted.len == 1);
    CHECK(sim.stats.creatures == sim.pool.count);
    for (int i = 0; i < 300; i++) {
        sim_step(&sim);
    }
    const Stats *st = &sim.stats;
    CHECK(st->tick == 300);
    int alive = 0;
    long veg = 0;
    for (int i = 0; i < sim.pool.capacity; i++) {
        alive += sim.pool.items[i].alive ? 1 : 0;
    }
    for (int i = 0; i < 64 * 32; i++) {
        veg += sim.world.veg[i];
    }
    CHECK(st->creatures == alive);
    CHECK(st->veg_total == veg);
    CHECK(st->by_level[BAND_PLANT] + st->by_level[BAND_MEAT] +
              st->by_level[BAND_TOP] ==
          st->creatures);
    CHECK(st->lineages == distinct_lineages(&sim.pool));
    CHECK(st->lineages >= 1);
    CHECK(sim.stats.hist_planted.len == HISTORY);
    sim_free(&sim);
}

static void test_sampled_every_n_ticks(void)
{
    Settings set = settings_default();
    set.stats_every = 10;
    Sim sim;
    CHECK(sim_init(&sim, &set, 7, 64, 32));
    for (int i = 0; i < 25; i++) {
        sim_step(&sim);
    }
    /* Samples at ticks 0, 10 and 20. */
    CHECK(sim.stats.tick == 20);
    CHECK(sim.stats.hist_planted.len == 3);
    /* A new seed starts the histories again. */
    CHECK(sim_reseed(&sim, 8));
    CHECK(sim.stats.tick == 0);
    CHECK(sim.stats.hist_planted.len == 1);
    sim_free(&sim);
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_bands();
    test_history_partly_filled();
    test_history_keeps_the_newest();
    test_sample_by_hand();
    test_sampled_while_running();
    test_sampled_every_n_ticks();
    TEST_SUMMARY();
}
