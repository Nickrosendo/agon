#include "test.h"

#include <math.h>  /* fabsf */
#include <stdio.h> /* snprintf */

#include "brain_script.h"
#include "family.h"
#include "settings.h"
#include "sim.h"

#define WORLD 64

static void make_empty_sim(Sim *s)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "script");
    set.start_share = 0.0f;
    set.min_creatures = 0;
    set.veg_start_chance = 0.0f;
    CHECK(sim_init(s, &set, 7, WORLD, WORLD));
    for (int i = 0; i < WORLD * WORLD; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
    }
}

/* Places one more creature of this family on the next free cell. */
static Creature *add(Sim *s, uint32_t lineage)
{
    int n = s->pool.count;
    int slot = pool_spawn(&s->pool, &s->world, n % WORLD, n / WORLD);
    CHECK(slot >= 0);
    s->pool.items[slot].lineage = lineage;
    return &s->pool.items[slot];
}

static void kill_all(Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            pool_kill(&s->pool, &s->world, i, &s->set, DEATH_WOUNDS);
        }
    }
}

/* One pass: move the clock on just far enough. */
static void pass(Families *f, Sim *s)
{
    s->tick = f->next_tick;
    family_update(f, s);
}

static const FamilyRecord *find(const Families *f, uint32_t lineage)
{
    for (int i = 0; i < f->used; i++) {
        if (f->records[i].lineage == lineage) {
            return &f->records[i];
        }
    }
    return NULL;
}

static void test_average_on_known_creatures(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    add(&s, 5)->genome.g[GENE_SIZE] = 0.2f;
    add(&s, 5)->genome.g[GENE_SIZE] = 0.4f;
    add(&s, 5)->genome.g[GENE_SIZE] = 0.6f;
    pass(&f, &s);
    const FamilyRecord *r = find(&f, 5);
    CHECK(r != NULL);
    CHECK(r->count == 3 && r->n == 1);
    CHECK(fabsf(r->samples[0].traits.size - 0.4f) < 0.001f);
    family_free(&f);
    sim_free(&s);
}

static void test_hue_average_across_wrap(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    add(&s, 5)->genome.g[GENE_HUE] = 0.99f;
    add(&s, 5)->genome.g[GENE_HUE] = 0.01f;
    pass(&f, &s);
    float hue = find(&f, 5)->samples[0].hue;
    CHECK(hue >= 0.0f && hue < 1.0f);
    CHECK(hue < 0.01f || hue > 0.99f); /* not 0.5 */
    family_free(&f);
    sim_free(&s);
}

static void test_halves_when_full(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    Creature *c = add(&s, 5);
    for (int p = 0; p < 33; p++) {
        /* The size tells the samples apart: pass number / 100. */
        c->genome.g[GENE_SIZE] = (float)p / 100.0f;
        pass(&f, &s);
    }
    const FamilyRecord *r = find(&f, 5);
    CHECK(r->n == 17);
    CHECK(r->every == 2);
    CHECK(fabsf(r->samples[0].traits.size - 0.00f) < 0.001f);  /* oldest kept */
    CHECK(fabsf(r->samples[1].traits.size - 0.02f) < 0.001f);  /* every second */
    CHECK(fabsf(r->samples[16].traits.size - 0.32f) < 0.001f); /* newest */
    family_free(&f);
    sim_free(&s);
}

static void test_strip_even_spacing(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    Creature *c = add(&s, 5);
    FamilySample out[FAMILY_STRIP];
    for (int p = 0; p < 3; p++) {
        c->genome.g[GENE_SIZE] = (float)p / 100.0f;
        pass(&f, &s);
    }
    CHECK(family_strip(&f, 5, out) == 3);
    CHECK(fabsf(out[2].traits.size - 0.02f) < 0.001f);
    for (int p = 3; p < 32; p++) {
        c->genome.g[GENE_SIZE] = (float)p / 100.0f;
        pass(&f, &s);
    }
    CHECK(find(&f, 5)->n == 32);
    CHECK(family_strip(&f, 5, out) == 8);
    CHECK(fabsf(out[0].traits.size - 0.00f) < 0.001f);
    CHECK(fabsf(out[7].traits.size - 0.31f) < 0.001f);
    /* Oldest first, and spaced at most a little unevenly. */
    for (int i = 1; i < 8; i++) {
        float step = out[i].traits.size - out[i - 1].traits.size;
        CHECK(step > 0.03f && step < 0.06f);
    }
    family_free(&f);
    sim_free(&s);
}

static void test_more_than_32_families(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    /* Lineage L has L members, for L from 1 to 40. */
    for (uint32_t lineage = 1; lineage <= 40; lineage++) {
        for (uint32_t k = 0; k < lineage; k++) {
            add(&s, lineage);
        }
    }
    pass(&f, &s);
    CHECK(f.used == FAMILY_MAX);
    for (uint32_t lineage = 1; lineage <= 8; lineage++) {
        CHECK(find(&f, lineage) == NULL);
    }
    for (uint32_t lineage = 9; lineage <= 40; lineage++) {
        const FamilyRecord *r = find(&f, lineage);
        CHECK(r != NULL && r->count == (int)lineage);
    }
    CHECK(family_largest(&f, &s) == 40);
    family_free(&f);
    sim_free(&s);
}

static void test_family_dying_out_is_forgotten(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    add(&s, 5);
    add(&s, 6);
    pass(&f, &s);
    CHECK(find(&f, 5) != NULL);
    for (int i = 0; i < s.pool.capacity; i++) {
        if (s.pool.items[i].alive && s.pool.items[i].lineage == 5) {
            pool_kill(&s.pool, &s.world, i, &s.set, DEATH_WOUNDS);
        }
    }
    pass(&f, &s);
    CHECK(find(&f, 5) == NULL);
    CHECK(find(&f, 6) != NULL && find(&f, 6)->n == 2);
    FamilySample out[FAMILY_STRIP];
    CHECK(family_strip(&f, 5, out) == 0);
    family_free(&f);
    sim_free(&s);
}

static void test_empty_world(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    pass(&f, &s);
    pass(&f, &s);
    CHECK(f.used == 0);
    CHECK(family_largest(&f, &s) == FAMILY_NONE);
    FamilySample out[FAMILY_STRIP];
    CHECK(family_strip(&f, 0, out) == 0);
    /* A record that outlived its family says nothing once nobody is alive. */
    add(&s, 5);
    pass(&f, &s);
    kill_all(&s);
    CHECK(family_largest(&f, &s) == FAMILY_NONE);
    family_free(&f);
    sim_free(&s);
}

static void test_reset_after_new_world(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    add(&s, 0)->genome.g[GENE_SIZE] = 0.9f;
    pass(&f, &s);
    kill_all(&s);
    family_reset(&f);
    CHECK(f.used == 0);
    /* A new family with the same id, starting from tick 0 again. */
    s.tick = 0;
    add(&s, 0)->genome.g[GENE_SIZE] = 0.1f;
    family_update(&f, &s);
    FamilySample out[FAMILY_STRIP];
    CHECK(family_strip(&f, 0, out) == 1);
    CHECK(fabsf(out[0].traits.size - 0.1f) < 0.001f);
    family_free(&f);
    sim_free(&s);
}

/* However many ticks a frame ran, one pass; and none until one is due. */
static void test_at_most_one_pass_per_call(void)
{
    Sim s;
    make_empty_sim(&s);
    Families f;
    CHECK(family_init(&f, s.pool.capacity));
    add(&s, 5);
    family_update(&f, &s);
    CHECK(f.passes == 1);
    family_update(&f, &s);
    CHECK(f.passes == 1);
    s.tick = FAMILY_PASS_TICKS - 1;
    family_update(&f, &s);
    CHECK(f.passes == 1);
    s.tick = 10 * FAMILY_PASS_TICKS;
    family_update(&f, &s);
    CHECK(f.passes == 2);
    family_free(&f);
    sim_free(&s);
}

static void test_free_twice(void)
{
    Families f;
    CHECK(family_init(&f, 8));
    family_free(&f);
    family_free(&f);
}

int main(void)
{
    CHECK(brain_register(&BRAIN_SCRIPT));
    test_average_on_known_creatures();
    test_hue_average_across_wrap();
    test_halves_when_full();
    test_strip_even_spacing();
    test_more_than_32_families();
    test_family_dying_out_is_forgotten();
    test_empty_world();
    test_reset_after_new_world();
    test_at_most_one_pass_per_call();
    test_free_twice();
    TEST_SUMMARY();
}
