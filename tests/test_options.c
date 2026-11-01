#include "test.h"

#include <string.h> /* strstr */

#include "options.h"

static bool size_ok(const char *text, int w, int h)
{
    int pw = -1, ph = -1;
    return parse_size(text, &pw, &ph) && pw == w && ph == h;
}

static bool size_rejected(const char *text)
{
    int w = -1, h = -1;
    /* Rejected, and the outputs left alone. */
    return !parse_size(text, &w, &h) && w == -1 && h == -1;
}

static void test_parse_size(void)
{
    CHECK(size_ok("80x40", 80, 40));
    CHECK(size_ok("1x1", 1, 1));
    CHECK(size_ok("4096x4096", OPTIONS_MAX_SIDE, OPTIONS_MAX_SIDE));
    CHECK(size_rejected("0x10"));
    CHECK(size_rejected("-5x10"));
    CHECK(size_rejected("80"));
    CHECK(size_rejected("80x"));
    CHECK(size_rejected("x40"));
    CHECK(size_rejected("80x40z"));
    CHECK(size_rejected("99999999x99999999"));
    CHECK(size_rejected(""));
    CHECK(size_rejected(" 80x40"));
    CHECK(size_rejected("80x 40"));
    CHECK(size_rejected("+80x40"));
    CHECK(size_rejected("4097x10"));
    CHECK(size_rejected("80X40"));
}

static void test_parse_seed(void)
{
    uint64_t seed = 0;
    CHECK(parse_seed("123", &seed) && seed == 123);
    CHECK(parse_seed("0x7b", &seed) && seed == 0x7b);
    CHECK(parse_seed("18446744073709551615", &seed) && seed == UINT64_MAX);
    CHECK(!parse_seed("18446744073709551616", &seed)); /* 2^64 */
    CHECK(!parse_seed("", &seed));
    CHECK(!parse_seed("-1", &seed));
    CHECK(!parse_seed(" 5", &seed));
    CHECK(!parse_seed("12a", &seed));
    CHECK(!parse_seed("banana", &seed));
}

static void test_parse_long(void)
{
    long v = 0;
    CHECK(parse_long("10", 1, 100, &v) && v == 10);
    CHECK(parse_long("-3", -5, 5, &v) && v == -3);
    CHECK(!parse_long("101", 1, 100, &v));
    CHECK(!parse_long("0", 1, 100, &v));
    CHECK(!parse_long("", 1, 100, &v));
    CHECK(!parse_long("5x", 1, 100, &v));
    CHECK(!parse_long("99999999999999999999", 1, 100, &v));
}

/* Runs options_parse on a NULL-ended list of arguments. The arguments
 * are string literals; argv is an array of pointers to them, which
 * getopt_long may reorder without ever writing into the strings. */
static bool parse(char **argv, Options *o, char *err, size_t size)
{
    int argc = 0;
    while (argv[argc] != NULL) {
        argc++;
    }
    return options_parse(argc, argv, o, err, size);
}

static void test_defaults(void)
{
    char *argv[] = {"agon", NULL};
    Options o;
    char err[128];
    CHECK(parse(argv, &o, err, sizeof(err)));
    CHECK(!o.seed_given && !o.size_given && !o.once && !o.no_floor);
    CHECK(o.speed == 0);
    CHECK(o.ticks == OPTIONS_DEFAULT_TICKS);
    CHECK(!o.help && !o.version);
}

static void test_every_option(void)
{
    char *argv[] = {"agon",    "--seed",  "0x10",       "--size", "80x40",
                    "--speed", "30",      "--once",     "--ticks", "500",
                    "--no-floor", NULL};
    Options o;
    char err[128];
    CHECK(parse(argv, &o, err, sizeof(err)));
    CHECK(o.seed_given && o.seed == 16);
    CHECK(o.size_given && o.width == 80 && o.height == 40);
    CHECK(o.speed == 30);
    CHECK(o.once && o.ticks == 500 && o.no_floor);

    /* --name=value works too, and so does -h for --help. */
    char *argv2[] = {"agon", "--seed=7", "-h", NULL};
    CHECK(parse(argv2, &o, err, sizeof(err)));
    CHECK(o.seed == 7 && o.help);
}

/* True when parsing fails with a message containing want. */
static bool fails_with(char **argv, const char *want)
{
    Options o;
    char err[128] = "";
    bool ok = parse(argv, &o, err, sizeof(err));
    return !ok && strstr(err, want) != NULL;
}

static void test_mistakes(void)
{
    char *unknown[] = {"agon", "--bogus", NULL};
    CHECK(fails_with(unknown, "--bogus"));
    char *unknown_short[] = {"agon", "-z", NULL};
    CHECK(fails_with(unknown_short, "-z"));
    char *missing[] = {"agon", "--seed", NULL};
    CHECK(fails_with(missing, "--seed"));
    char *bad_seed[] = {"agon", "--seed", "banana", NULL};
    CHECK(fails_with(bad_seed, "banana"));
    char *bad_size[] = {"agon", "--size", "0x0", NULL};
    CHECK(fails_with(bad_size, "0x0"));
    char *bad_speed[] = {"agon", "--speed", "0", NULL};
    CHECK(fails_with(bad_speed, "speed"));
    char *bad_ticks[] = {"agon", "--ticks", "-1", NULL};
    CHECK(fails_with(bad_ticks, "ticks"));
    char *extra[] = {"agon", "world", NULL};
    CHECK(fails_with(extra, "world"));
}

static void test_parse_twice(void)
{
    /* getopt_long keeps its place in global variables. A second parse
     * must start from the beginning again, not where the first ended. */
    char *first[] = {"agon", "--seed", "1", NULL};
    char *second[] = {"agon", "--seed", "2", NULL};
    Options o;
    char err[128];
    CHECK(parse(first, &o, err, sizeof(err)) && o.seed == 1);
    CHECK(parse(second, &o, err, sizeof(err)) && o.seed == 2);
}

int main(void)
{
    test_parse_size();
    test_parse_seed();
    test_parse_long();
    test_defaults();
    test_every_option();
    test_mistakes();
    test_parse_twice();
    TEST_SUMMARY();
}
