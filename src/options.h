/*
 * options: the command line. Parses argv into an Options struct and
 * checks every value; prints nothing itself, so tests can run it. main
 * decides what to print and which exit code to return.
 */
#ifndef AGON_OPTIONS_H
#define AGON_OPTIONS_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

/* The largest world width or height --size accepts. */
#define OPTIONS_MAX_SIDE 4096
/* --once runs this many ticks unless --ticks says otherwise. */
#define OPTIONS_DEFAULT_TICKS 2000
#define OPTIONS_MAX_TICKS 100000000L
/* The range --speed accepts, in ticks per second. */
#define OPTIONS_MAX_SPEED 1000

typedef struct {
    bool seed_given;
    uint64_t seed;
    bool size_given;
    int width, height; /* world cells */
    int speed;         /* ticks per second; 0 when not given */
    bool once;
    long ticks;        /* how far --once runs */
    bool no_floor;
    bool help, version;
} Options;

/* Parses "WxH", two whole numbers from 1 to OPTIONS_MAX_SIDE joined by
 * an 'x', with nothing before, between or after. False for anything
 * else; *w and *h are then unchanged. */
bool parse_size(const char *text, int *w, int *h);

/* Parses a whole number ("123") or 0x hexadecimal ("0x7b") that fits a
 * uint64_t. No sign, no spaces, nothing after it. */
bool parse_seed(const char *text, uint64_t *seed);

/* Parses a decimal whole number from min to max, nothing before or
 * after it but an optional '-' sign. */
bool parse_long(const char *text, long min, long max, long *out);

/* Fills *o from the command line. False on any mistake, with a one-line
 * message (no newline) in err, a buffer of err_size bytes. argv's
 * entries may be reordered (getopt_long moves options before other
 * arguments); the strings themselves are not changed. */
bool options_parse(int argc, char **argv, Options *o, char *err,
                   size_t err_size);

/* The text --help prints. */
extern const char OPTIONS_USAGE[];

#endif
