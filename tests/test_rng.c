#include "test.h"

#include "rng.h"

static void test_splitmix64_known_values(void)
{
    /* Published first outputs of splitmix64 from state 0. If these match,
     * the shifts, multiplications and wrap-around all behave as in the
     * reference implementation. */
    uint64_t state = 0;
    CHECK(rng_splitmix64(&state) == 0xE220A8397B1DCDAFULL);
    CHECK(rng_splitmix64(&state) == 0x6E789E6AA1B965F4ULL);
    CHECK(rng_splitmix64(&state) == 0x06C45D188009454FULL);
}

static void test_same_seed_same_sequence(void)
{
    Rng a, b;
    rng_seed(&a, 12345);
    rng_seed(&b, 12345);
    int all_equal = 1;
    for (int i = 0; i < 1000; i++) {
        if (rng_next(&a) != rng_next(&b)) {
            all_equal = 0;
        }
    }
    CHECK(all_equal);

    Rng one, two;
    rng_seed(&one, 1);
    rng_seed(&two, 2);
    CHECK(rng_next(&one) != rng_next(&two));
}

static void test_below_is_in_range_and_even(void)
{
    Rng r;
    rng_seed(&r, 7);
    int buckets[7] = {0};
    int all_in_range = 1;
    for (int i = 0; i < 70000; i++) {
        uint32_t v = rng_below(&r, 7);
        if (v >= 7) {
            all_in_range = 0;
            continue; /* do not index outside buckets */
        }
        buckets[v]++;
    }
    CHECK(all_in_range);
    for (int b = 0; b < 7; b++) {
        CHECK(buckets[b] >= 9500 && buckets[b] <= 10500);
    }
}

static void test_unit_range_and_mean(void)
{
    Rng r;
    rng_seed(&r, 99);
    double sum = 0.0;
    int all_in_range = 1;
    for (int i = 0; i < 100000; i++) {
        float v = rng_unit(&r);
        if (v < 0.0f || v >= 1.0f) {
            all_in_range = 0;
        }
        sum += v;
    }
    double mean = sum / 100000.0;
    CHECK(all_in_range);
    CHECK(mean > 0.49 && mean < 0.51);
}

static void test_normal_mean_and_variance(void)
{
    Rng r;
    rng_seed(&r, 2024);
    const int n = 100000;
    /* Sums in double: adding 100,000 floats into a float loses precision
     * as the sum grows. */
    double sum = 0.0, sum_squares = 0.0;
    for (int i = 0; i < n; i++) {
        double v = rng_normal(&r);
        sum += v;
        sum_squares += v * v;
    }
    double mean = sum / n;
    double variance = sum_squares / n - mean * mean;
    CHECK(mean > -0.02 && mean < 0.02);
    CHECK(variance > 0.95 && variance < 1.05);
}

int main(void)
{
    test_splitmix64_known_values();
    test_same_seed_same_sequence();
    test_below_is_in_range_and_even();
    test_unit_range_and_mean();
    test_normal_mean_and_variance();
    TEST_SUMMARY();
}
