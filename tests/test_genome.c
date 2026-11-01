#include "test.h"

#include <math.h>    /* isfinite, fabsf */
#include <stdbool.h>

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
    genome_copy_mutated(&child, &parent, &r, &set);
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
        genome_copy_mutated(&child, &parent, &r, &set);
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
        genome_copy_mutated(&child, &parent, &r, &set);
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
        genome_copy_mutated(&child, &a, &r, &set);
        if (genome_kinship(&a, &child) > 0.9f) {
            close++;
        }
        CHECK(genome_kinship(&a, &child) == genome_kinship(&child, &a));
    }
    CHECK(close == 100);
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

int main(void)
{
    test_random_in_range();
    test_rate_zero_copies_exactly();
    test_rate_one_mutates_within_range();
    test_hue_wraps();
    test_kinship();
    test_weight_positions();
    TEST_SUMMARY();
}
