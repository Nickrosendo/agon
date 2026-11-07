#include "years.h"

#include <inttypes.h> /* PRIu64 */
#include <limits.h>   /* INT_MAX */
#include <stdio.h>    /* snprintf */

/* The settings table keeps this at YEARS_MIN_LENGTH or more. Checked again
 * because % by zero is undefined behaviour in C (it usually ends the program),
 * and a test or a future caller could hand these functions anything. */
static uint64_t safe_length(int ticks_per_year)
{
    return ticks_per_year >= YEARS_MIN_LENGTH ? (uint64_t)ticks_per_year
                                              : (uint64_t)YEARS_MIN_LENGTH;
}

int years_season_of(uint64_t ticks, int ticks_per_year)
{
    uint64_t length = safe_length(ticks_per_year);
    /* left is below length, so left * 4 / length is always 0 to 3: a season.
     * left fits an int, so left * 4 cannot overflow 64 bits. Tick 0 of a year
     * gives 0, spring, whatever the length. */
    uint64_t left = ticks % length;
    return (int)(left * SEASON_COUNT / length);
}

float years_phase(uint64_t ticks, int ticks_per_year)
{
    uint64_t length = safe_length(ticks_per_year);
    /* Divided as doubles: two integers divided would give 0 every time. The
     * result is below 1 as a double, and length is at most a million, so even
     * the nearest float to it stays below 1. */
    return (float)((double)(ticks % length) / (double)length);
}

bool years_season_starts(uint64_t ticks, int ticks_per_year, int season)
{
    if (years_season_of(ticks, ticks_per_year) != season) {
        return false;
    }
    /* The first tick of a year starts spring; any other tick starts a season
     * when the tick before it was in another. ticks is above 0 in that case,
     * so ticks - 1 cannot wrap round. */
    uint64_t left = ticks % safe_length(ticks_per_year);
    return left == 0 || years_season_of(ticks - 1, ticks_per_year) != season;
}

void years_format(char *out, size_t len, uint64_t ticks, int ticks_per_year)
{
    uint64_t length = safe_length(ticks_per_year);
    uint64_t whole = ticks / length;
    /* Whole numbers only: a double holds about 15 to 16 digits, so a very large
     * tick count would lose its last ones. left is below length, which fits an
     * int, so left * 10 cannot overflow 64 bits. */
    uint64_t left = ticks % length;
    uint64_t tenth = left * 10u / length;
    if (tenth == 0) {
        snprintf(out, len, "%" PRIu64 " y", whole);
    } else {
        snprintf(out, len, "%" PRIu64 ".%" PRIu64 " y", whole, tenth);
    }
}

const char *years_season(uint64_t ticks, int ticks_per_year)
{
    /* static: one table for the whole program, not rebuilt on every call. The
     * pointers it holds point at string literals, which last as long as the
     * program, so returning one is safe, unlike a pointer to a local array. */
    static const char *const NAMES[SEASON_COUNT] = {"spring", "summer", "autumn",
                                                    "winter"};
    return NAMES[years_season_of(ticks, ticks_per_year)];
}

int years_whole(uint64_t ticks, int ticks_per_year)
{
    uint64_t whole = ticks / safe_length(ticks_per_year);
    return whole > (uint64_t)INT_MAX ? INT_MAX : (int)whole;
}
