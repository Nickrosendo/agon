#include "test.h"

#include <math.h>    /* isfinite, fabsf */
#include <stdbool.h>
#include <string.h>  /* memcmp */

#include "genome.h"
#include "rng.h"
#include "settings.h"

/* True when every value is finite, every trait in 0..1 (hue below 1) and
 * every weight within the limit. */
static bool genome_in_range(const Genome *g, float limit)
{
    for (int i = 0; i < GENOME_LEN; i++) {
        if (!isfinite(g->g[i])) {
            return false;
        }
    }
    for (int i = 0; i < GENE_TRAITS; i++) {
        if (g->g[i] < 0.0f || g->g[i] > 1.0f) {
            return false;
        }
    }
    if (g->g[GENE_HUE] >= 1.0f) {
        return false;
    }
    for (int i = GENE_TRAITS; i < GENOME_LEN; i++) {
        if (fabsf(g->g[i]) > limit) {
            return false;
        }
    }
    return true;
}

static void test_random_in_range(void)
{
    Settings set = settings_default();
    Rng r;
    rng_seed(&r, 1);
    int bad = 0;
    for (int k = 0; k < 1000; k++) {
        Genome g;
        genome_random(&g, &r, &set);
        if (!genome_in_range(&g, set.weight_limit)) {
            bad++;
        }
    }
    CHECK(bad == 0);
}

static void test_rate_zero_copies_exactly(void)
{
    Settings set = settings_default();
    set.mutation_rate = 0.0f;
    Rng r;
    rng_seed(&r, 2);
    Genome parent, child;
    genome_random(&parent, &r, &set);
    genome_cross(&child, &parent, &parent, &r, &set);
    int differ = 0;
    for (int i = 0; i < GENOME_LEN; i++) {
        /* Exact float comparison on purpose: a copy must be bit for bit. */
        if (child.g[i] != parent.g[i]) {
            differ++;
        }
    }
    CHECK(differ == 0);
}

static void test_rate_one_mutates_within_range(void)
{
    Settings set = settings_default();
    set.mutation_rate = 1.0f;
    set.mutation_spread = 0.1f;
    Rng r;
    rng_seed(&r, 3);
    Genome parent;
    genome_random(&parent, &r, &set);
    int bad = 0;
    long differ = 0;
    for (int k = 0; k < 1000; k++) {
        Genome child;
        genome_cross(&child, &parent, &parent, &r, &set);
        if (!genome_in_range(&child, set.weight_limit)) {
            bad++;
        }
        for (int i = 0; i < GENOME_LEN; i++) {
            if (child.g[i] != parent.g[i]) {
                differ++;
            }
        }
    }
    CHECK(bad == 0);
    CHECK(differ >= 1000L * GENOME_LEN * 9 / 10);
}

static void test_hue_wraps(void)
{
    Settings set = settings_default();
    set.mutation_rate = 1.0f;
    set.mutation_spread = 0.1f;
    Rng r;
    rng_seed(&r, 4);
    Genome parent;
    genome_random(&parent, &r, &set);
    parent.g[GENE_HUE] = 0.99f;
    int wrapped = 0, out_of_range = 0;
    for (int k = 0; k < 1000; k++) {
        Genome child;
        genome_cross(&child, &parent, &parent, &r, &set);
        float hue = child.g[GENE_HUE];
        if (hue < 0.0f || hue >= 1.0f) {
            out_of_range++;
        }
        /* Clamping would pile children up at 1.0; wrapping sends the
         * upward half round to just above 0. */
        if (hue < 0.5f) {
            wrapped++;
        }
    }
    CHECK(out_of_range == 0);
    CHECK(wrapped > 200);
}

/* Adding the instinct sets exactly the recipe's weights, draws no random
 * number, and keeps every value within its limits. */
static void test_instinct_recipe(void)
{
    Settings set = settings_default();
    int index[INSTINCT_MAX];
    float value[INSTINCT_MAX];
    int n = genome_instinct(index, value, INSTINCT_MAX);
    CHECK(n > 10 && n <= INSTINCT_MAX);
    Genome g = (Genome){0};
    Rng r;
    rng_seed(&r, 4);
    Rng before = r;
    genome_add_instinct(&g, &set);
    CHECK(memcmp(&r, &before, sizeof(r)) == 0); /* nothing drawn */
    int set_count = 0;
    for (int i = 0; i < GENOME_LEN; i++) {
        set_count += g.g[i] != 0.0f;
    }
    CHECK(set_count == n); /* each listed once, nothing else touched */
    for (int k = 0; k < n; k++) {
        CHECK(index[k] >= GENE_TRAITS && index[k] < GENOME_LEN); /* weights only */
        CHECK(g.g[index[k]] == value[k]);
    }
    /* On top of weights already near the limit, it stays within it. */
    for (int i = GENE_TRAITS; i < GENOME_LEN; i++) {
        g.g[i] = set.weight_limit;
    }
    genome_add_instinct(&g, &set);
    for (int i = GENE_TRAITS; i < GENOME_LEN; i++) {
        CHECK(g.g[i] <= set.weight_limit && g.g[i] >= -set.weight_limit);
    }
}

static void test_kinship(void)
{
    Settings set = settings_default();
    Rng r;
    rng_seed(&r, 5);
    Genome a, b, child;
    genome_random(&a, &r, &set);
    genome_random(&b, &r, &set);
    CHECK(genome_kinship(&a, &a) == 1.0f);
    CHECK(genome_kinship(&a, &b) < 0.6f);
    CHECK(genome_kinship(&a, &b) == genome_kinship(&b, &a));

    int close = 0;
    for (int k = 0; k < 100; k++) {
        genome_cross(&child, &a, &a, &r, &set);
        if (genome_kinship(&a, &child) > 0.9f) {
            close++;
        }
        CHECK(genome_kinship(&a, &child) == genome_kinship(&child, &a));
    }
    CHECK(close == 100);
}

/* Parents that can be told apart at a glance: traits and weights all one
 * value in a, another in b. */
static void two_parents(Genome *a, Genome *b)
{
    for (int i = 0; i < GENOME_LEN; i++) {
        bool trait = i < GENE_TRAITS;
        a->g[i] = trait ? 0.25f : 1.0f;
        b->g[i] = trait ? 0.75f : -1.0f;
    }
}

static void test_cross_takes_each_trait_from_a_parent(void)
{
    Settings set = settings_default();
    set.mutation_rate = 0.0f;
    Rng r;
    rng_seed(&r, 11);
    Genome a, b, child;
    two_parents(&a, &b);
    int strangers = 0;
    int from_a = 0;
    int from_b = 0;
    for (int k = 0; k < 200; k++) {
        genome_cross(&child, &a, &b, &r, &set);
        for (int i = 0; i < GENE_TRAITS; i++) {
            if (child.g[i] == a.g[i]) {
                from_a++;
            } else if (child.g[i] == b.g[i]) {
                from_b++;
            } else {
                strangers++;
            }
        }
    }
    CHECK(strangers == 0);
    CHECK(from_a > 0 && from_b > 0);
}

/* The sign every weight of hidden value h has, or 0 when they disagree. */
static int hidden_group_sign(const Genome *g, int h)
{
    int sign = g->g[net_hidden_weight(h, 0)] > 0.0f ? 1 : -1;
    for (int i = 0; i <= NET_IN; i++) {
        if ((g->g[net_hidden_weight(h, i)] > 0.0f ? 1 : -1) != sign) {
            return 0;
        }
    }
    for (int o = 0; o < NET_OUT; o++) {
        if ((g->g[net_out_weight(o, h)] > 0.0f ? 1 : -1) != sign) {
            return 0;
        }
    }
    return sign;
}

static void test_cross_keeps_hidden_groups_whole(void)
{
    Settings set = settings_default();
    set.mutation_rate = 0.0f;
    Rng r;
    rng_seed(&r, 12);
    Genome a, b, child;
    two_parents(&a, &b);
    int broken = 0;
    int plus = 0;
    int minus = 0;
    for (int k = 0; k < 200; k++) {
        genome_cross(&child, &a, &b, &r, &set);
        for (int h = 0; h < NET_HIDDEN; h++) {
            int sign = hidden_group_sign(&child, h);
            broken += sign == 0;
            plus += sign > 0;
            minus += sign < 0;
        }
    }
    CHECK(broken == 0);
    CHECK(plus > 0 && minus > 0); /* the groups do come from both parents */
}

static void test_cross_child_can_differ_from_both(void)
{
    Settings set = settings_default();
    set.mutation_rate = 1.0f;
    Rng r;
    rng_seed(&r, 13);
    Genome a, b, child;
    two_parents(&a, &b);
    int moved = 0;
    int bad = 0;
    for (int k = 0; k < 100; k++) {
        genome_cross(&child, &a, &b, &r, &set);
        if (!genome_in_range(&child, set.weight_limit)) {
            bad++;
        }
        for (int i = 0; i < GENE_TRAITS; i++) {
            moved += child.g[i] != a.g[i] && child.g[i] != b.g[i];
        }
    }
    CHECK(bad == 0);
    CHECK(moved > 0);
}

/* Equal seeds and different parents must leave the generator in one state:
 * otherwise what is in a genome would change every later random number. */
static void test_cross_draws_the_same_count(void)
{
    Settings set = settings_default();
    Rng seed_rng, one, two;
    rng_seed(&seed_rng, 14);
    rng_seed(&one, 15);
    rng_seed(&two, 15);
    Genome a, b, c, d, child;
    genome_random(&a, &seed_rng, &set);
    genome_random(&b, &seed_rng, &set);
    genome_random(&c, &seed_rng, &set);
    genome_random(&d, &seed_rng, &set);
    genome_cross(&child, &a, &b, &one, &set);
    genome_cross(&child, &c, &d, &two, &set);
    CHECK(memcmp(one.s, two.s, sizeof(one.s)) == 0);
}

static void test_body_likeness(void)
{
    Settings set = settings_default();
    Rng r;
    rng_seed(&r, 16);
    Genome a, b;
    genome_random(&a, &r, &set);
    b = a;
    CHECK(genome_body_likeness(&a, &b) == 1.0f);
    /* Opposite weights change nothing: only the body counts. */
    for (int i = GENE_TRAITS; i < GENOME_LEN; i++) {
        b.g[i] = -a.g[i];
    }
    CHECK(genome_body_likeness(&a, &b) == 1.0f);
    /* Opposite bodies: 0 and 1 on every trait, half way round in hue. */
    for (int i = 0; i < GENE_TRAITS; i++) {
        a.g[i] = 0.0f;
        b.g[i] = 1.0f;
    }
    a.g[GENE_HUE] = 0.0f;
    b.g[GENE_HUE] = 0.5f;
    CHECK(genome_body_likeness(&a, &b) < 0.5f);
    CHECK(genome_body_likeness(&a, &b) == genome_body_likeness(&b, &a));
    /* 0.98 and 0.01 are close on the wheel, so nearly alike in hue. */
    b = a;
    a.g[GENE_HUE] = 0.98f;
    b.g[GENE_HUE] = 0.01f;
    CHECK(genome_body_likeness(&a, &b) > 0.98f);
}

static void test_weight_positions(void)
{
    /* The layout functions give every weight its own place, all inside
     * the genome and after the traits. */
    int seen[GENOME_LEN] = {0};
    for (int h = 0; h < NET_HIDDEN; h++) {
        for (int i = 0; i <= NET_IN; i++) {
            seen[net_hidden_weight(h, i)]++;
        }
    }
    for (int o = 0; o < NET_OUT; o++) {
        for (int j = 0; j <= NET_HIDDEN; j++) {
            seen[net_out_weight(o, j)]++;
        }
    }
    int wrong = 0;
    for (int i = 0; i < GENOME_LEN; i++) {
        int expected = i < GENE_TRAITS ? 0 : 1;
        if (seen[i] != expected) {
            wrong++;
        }
    }
    CHECK(wrong == 0);
}

/* Sum over many children of how far the traits and the weights moved from
 * the parent, with every value mutating. */
static void mutation_distance(const Settings *set, float *traits, float *weights)
{
    Rng r;
    rng_seed(&r, 11);
    Genome parent;
    Settings start = settings_default();
    genome_random(&parent, &r, &start);
    for (int i = 0; i < GENE_TRAITS; i++) {
        parent.g[i] = 0.5f; /* away from the ends, so clamping cuts nothing */
    }
    *traits = 0.0f;
    *weights = 0.0f;
    for (int k = 0; k < 500; k++) {
        Genome child;
        genome_cross(&child, &parent, &parent, &r, set);
        for (int i = 0; i < GENOME_LEN; i++) {
            float d = fabsf(child.g[i] - parent.g[i]);
            if (i < GENE_TRAITS) {
                *traits += d;
            } else {
                *weights += d;
            }
        }
    }
}

/* The wildness scale widens the body traits' steps and leaves the weights'
 * steps alone, draw for draw. */
static void test_scale_widens_traits_only(void)
{
    Settings set = settings_default();
    set.mutation_rate = 1.0f;
    set.mutation_spread = 0.02f;
    float traits1, weights1, traits4, weights4;
    mutation_distance(&set, &traits1, &weights1);
    set.trait_mutation_scale = 4.0f;
    mutation_distance(&set, &traits4, &weights4);
    CHECK(traits4 > 3.0f * traits1);
    CHECK(weights4 == weights1); /* the same draws, so the very same sum */
}

/* With jumps on for size and every mutation a jump, a child's size can land
 * anywhere in 0..1; the other traits still only step. */
static void test_jump_draws_whole_range(void)
{
    Settings set = settings_default();
    set.mutation_rate = 1.0f;
    set.mutation_spread = 0.001f;
    set.jump_genes = 1 << GENE_SIZE;
    set.jump_chance = 1.0f;
    Rng r;
    rng_seed(&r, 12);
    Genome parent;
    genome_random(&parent, &r, &set);
    parent.g[GENE_SIZE] = 0.5f;
    parent.g[GENE_ARMOUR] = 0.5f;
    bool low = false, high = false, armour_stepped = true;
    for (int k = 0; k < 2000; k++) {
        Genome child;
        genome_cross(&child, &parent, &parent, &r, &set);
        low = low || child.g[GENE_SIZE] < 0.1f;
        high = high || child.g[GENE_SIZE] > 0.9f;
        armour_stepped =
            armour_stepped && fabsf(child.g[GENE_ARMOUR] - 0.5f) < 0.01f;
    }
    CHECK(low && high);
    CHECK(armour_stepped);
}

/* A jump chance with no gene's bit set changes nothing at all. */
static void test_no_jump_without_bit(void)
{
    Settings plain = settings_default();
    plain.mutation_rate = 0.3f;
    Settings chance = plain;
    chance.jump_chance = 1.0f;
    Rng ra, rb;
    rng_seed(&ra, 13);
    rng_seed(&rb, 13);
    Genome parent, a, b;
    genome_random(&parent, &ra, &plain);
    genome_random(&parent, &rb, &plain);
    genome_cross(&a, &parent, &parent, &ra, &plain);
    genome_cross(&b, &parent, &parent, &rb, &chance);
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

int main(void)
{
    test_scale_widens_traits_only();
    test_jump_draws_whole_range();
    test_no_jump_without_bit();
    test_random_in_range();
    test_rate_zero_copies_exactly();
    test_rate_one_mutates_within_range();
    test_hue_wraps();
    test_kinship();
    test_instinct_recipe();
    test_cross_takes_each_trait_from_a_parent();
    test_cross_keeps_hidden_groups_whole();
    test_cross_child_can_differ_from_both();
    test_cross_draws_the_same_count();
    test_body_likeness();
    test_weight_positions();
    TEST_SUMMARY();
}
