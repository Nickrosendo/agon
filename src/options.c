#include "options.h"

#include <errno.h>
#include <getopt.h> /* getopt_long, struct option, optind, optarg (GNU) */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* strtol, strtoull */

const char OPTIONS_USAGE[] =
    "usage: agon [options]\n"
    "  --seed N      the world's seed: a whole number, or 0x hexadecimal\n"
    "  --size WxH    world size in cells (default: fit the window;\n"
    "                80x40 with --once)\n"
    "  --speed N     ticks per second, rounded down to 1, 2, 5, 10, 15,\n"
    "                30, 60 or 120 (default 2)\n"
    "  --once        print one snapshot after --ticks ticks and exit\n"
    "  --ticks N     ticks to run before the snapshot (default 2000)\n"
    "  --no-floor    turn off a floor a settings file set\n"
    "  --config PATH read settings from a file (see --settings)\n"
    "  --save PATH   write the world there on quit and on the s key\n"
    "                (with --once: after the last tick)\n"
    "  --load PATH   resume a saved world; it brings its own seed, size and\n"
    "                settings, so not with --seed, --size, --config or\n"
    "                --no-floor\n"
    "  --settings    print every setting and its default, in the form\n"
    "                --config reads, and exit\n"
    "  --checksum    print a fingerprint of the world on leaving\n"
    "  -h, --help    this text\n"
    "  --version     the version\n"
    "keys: space pause, +/- speed, r new seed, i inspect, s save, q quit\n";

static bool is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/*
 * One side of a size: digits at text, which must stop at the character
 * stop. strtol is told base 10: base 0 would read "0x10" as hexadecimal
 * 16, so "0x10" would pass as a width. *end is set to where it stopped.
 */
static bool parse_side(const char *text, char stop, int *side,
                       const char **end)
{
    if (!is_digit(text[0])) {
        return false; /* empty, a sign, or a space: strtol would skip it */
    }
    char *after;
    errno = 0;
    long v = strtol(text, &after, 10);
    if (errno == ERANGE || *after != stop || v < 1 || v > OPTIONS_MAX_SIDE) {
        return false;
    }
    *side = (int)v; /* safe: 1 .. OPTIONS_MAX_SIDE fits in an int */
    *end = after;
    return true;
}

bool parse_size(const char *text, int *w, int *h)
{
    int width, height;
    const char *end;
    /* Into locals first, so a half-good text ("80x0") leaves *w alone. */
    if (!parse_side(text, 'x', &width, &end) ||
        !parse_side(end + 1, '\0', &height, &end)) {
        return false;
    }
    *w = width;
    *h = height;
    return true;
}

/*
 * strtoull alone is too forgiving: it skips leading spaces, accepts "-1"
 * (and turns it into 2^64 - 1), stops quietly at the first bad character,
 * and reports a too-large number only through errno. Each is checked.
 */
bool parse_seed(const char *text, uint64_t *seed)
{
    if (!is_digit(text[0])) {
        return false;
    }
    char *end;
    errno = 0;
    /* Base 0: decimal, or hexadecimal after 0x. */
    unsigned long long value = strtoull(text, &end, 0);
    if (errno == ERANGE || *end != '\0') {
        return false;
    }
    *seed = value;
    return true;
}

bool parse_long(const char *text, long min, long max, long *out)
{
    const char *digits = text[0] == '-' ? text + 1 : text;
    if (!is_digit(digits[0])) {
        return false;
    }
    char *end;
    errno = 0;
    long v = strtol(text, &end, 10);
    if (errno == ERANGE || *end != '\0' || v < min || v > max) {
        return false;
    }
    *out = v;
    return true;
}

/* Values for the long options that have no one-letter form: anything
 * outside the range of a char, so they cannot clash with a letter. */
enum {
    OPT_SEED = 256,
    OPT_SIZE,
    OPT_SPEED,
    OPT_ONCE,
    OPT_TICKS,
    OPT_NO_FLOOR,
    OPT_VERSION,
    OPT_CONFIG,
    OPT_SAVE,
    OPT_LOAD,
    OPT_SETTINGS,
    OPT_CHECKSUM,
    OPT_FRAMES,
    OPT_SCREENSHOT,
    OPT_ZOOM,
    OPT_KEYS,
    OPT_PAUSED
};

/* The table getopt_long reads: name, whether a value follows, a flag
 * pointer we do not use (NULL), and the value getopt_long returns for
 * it. The all-zero entry marks the end, as '\0' ends a string. */
static const struct option LONG_OPTIONS[] = {
    {"seed", required_argument, NULL, OPT_SEED},
    {"size", required_argument, NULL, OPT_SIZE},
    {"speed", required_argument, NULL, OPT_SPEED},
    {"once", no_argument, NULL, OPT_ONCE},
    {"ticks", required_argument, NULL, OPT_TICKS},
    {"no-floor", no_argument, NULL, OPT_NO_FLOOR},
    {"help", no_argument, NULL, 'h'},
    {"version", no_argument, NULL, OPT_VERSION},
    {"config", required_argument, NULL, OPT_CONFIG},
    {"save", required_argument, NULL, OPT_SAVE},
    {"load", required_argument, NULL, OPT_LOAD},
    {"settings", no_argument, NULL, OPT_SETTINGS},
    {"checksum", no_argument, NULL, OPT_CHECKSUM},
    {"frames", required_argument, NULL, OPT_FRAMES},
    {"screenshot", required_argument, NULL, OPT_SCREENSHOT},
    {"zoom", required_argument, NULL, OPT_ZOOM},
    {"keys", required_argument, NULL, OPT_KEYS},
    {"paused", no_argument, NULL, OPT_PAUSED},
    {NULL, 0, NULL, 0},
};

/* Checks and stores the value of one option that takes a value. */
static bool take_value(int opt, const char *value, Options *o, char *err,
                       size_t err_size)
{
    long n;
    switch (opt) {
    case OPT_CONFIG:
        o->config_path = value;
        return true;
    case OPT_SAVE:
        o->save_path = value;
        return true;
    case OPT_LOAD:
        o->load_path = value;
        return true;
    case OPT_SCREENSHOT:
        o->screenshot_path = value;
        return true;
    case OPT_ZOOM:
        o->zoom_name = value;
        return true;
    case OPT_PAUSED:
        o->paused = true;
        return true;
    case OPT_KEYS:
        o->keys = value;
        return true;
    case OPT_FRAMES:
        if (!parse_long(value, 1, OPTIONS_MAX_FRAMES, &n)) {
            snprintf(err, err_size, "--frames needs a whole number from 1 to %ld",
                     OPTIONS_MAX_FRAMES);
            return false;
        }
        o->frames = n;
        return true;
    case OPT_SEED:
        o->seed_given = parse_seed(value, &o->seed);
        if (!o->seed_given) {
            snprintf(err, err_size,
                     "'%s' is not a seed (a whole number, decimal or 0x "
                     "hexadecimal)",
                     value);
        }
        return o->seed_given;
    case OPT_SIZE:
        o->size_given = parse_size(value, &o->width, &o->height);
        if (!o->size_given) {
            snprintf(err, err_size,
                     "'%s' is not a size (WxH, each 1 to %d, e.g. 80x40)",
                     value, OPTIONS_MAX_SIDE);
        }
        return o->size_given;
    case OPT_SPEED:
        if (!parse_long(value, 1, OPTIONS_MAX_SPEED, &n)) {
            snprintf(err, err_size, "--speed needs a whole number from 1 to %d",
                     OPTIONS_MAX_SPEED);
            return false;
        }
        o->speed = (int)n;
        return true;
    default: /* OPT_TICKS */
        if (!parse_long(value, 0, OPTIONS_MAX_TICKS, &n)) {
            snprintf(err, err_size, "--ticks needs a whole number from 0 to %ld",
                     OPTIONS_MAX_TICKS);
            return false;
        }
        o->ticks = n;
        return true;
    }
}

bool options_parse(int argc, char **argv, Options *o, char *err,
                   size_t err_size)
{
    *o = (Options){.ticks = OPTIONS_DEFAULT_TICKS};
    if (err_size > 0) {
        err[0] = '\0';
    }
    /*
     * getopt_long keeps its state in global variables: optind is the
     * next argument to look at, optarg the current option's value. Set
     * optind to 0 to make glibc start over, so a second call (as in the
     * tests) does not carry on from where the first stopped. opterr = 0
     * stops it printing its own messages; we write err instead.
     */
    optind = 0;
    opterr = 0;
    /* The leading ':' makes a missing value come back as ':' instead of
     * '?', so the two mistakes get different messages. */
    const char *short_options = ":h";
    for (;;) {
        int opt = getopt_long(argc, argv, short_options, LONG_OPTIONS, NULL);
        if (opt == -1) {
            break; /* no more options */
        }
        switch (opt) {
        case 'h':
            o->help = true;
            break;
        case OPT_VERSION:
            o->version = true;
            break;
        case OPT_ONCE:
            o->once = true;
            break;
        case OPT_NO_FLOOR:
            o->no_floor = true;
            break;
        case OPT_SETTINGS:
            o->list_settings = true;
            break;
        case OPT_CHECKSUM:
            o->checksum = true;
            break;
        case ':':
            /* optind has moved past the option missing its value. */
            snprintf(err, err_size, "%s needs a value", argv[optind - 1]);
            return false;
        case '?':
            /* optopt is the unknown letter for a short option, 0 for an
             * unknown long one; argv[optind - 1] then holds it. */
            if (optopt != 0) {
                snprintf(err, err_size, "unknown option '-%c'", optopt);
            } else {
                snprintf(err, err_size, "unknown option '%s'",
                         argv[optind - 1]);
            }
            return false;
        default:
            if (!take_value(opt, optarg, o, err, err_size)) {
                return false;
            }
        }
    }
    /* getopt_long moved every non-option to the end; any left over is a
     * mistake: agon takes no file names or other bare words. */
    if (optind < argc) {
        snprintf(err, err_size, "unexpected argument '%s'", argv[optind]);
        return false;
    }
    /* A saved world brings its own seed, size and settings; taking any of
     * these as well would have to mean ignoring one of the two. */
    if (o->load_path != NULL &&
        (o->seed_given || o->size_given || o->config_path != NULL ||
         o->no_floor)) {
        snprintf(err, err_size, "--load brings its own seed, size and "
                                "settings: not with --seed, --size, --config "
                                "or --no-floor");
        return false;
    }
    return true;
}
