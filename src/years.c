#include "years.h"

#include <inttypes.h> /* PRIu64 */
#include <limits.h>   /* INT_MAX */
#include <stdio.h>    /* snprintf */

/* The settings table keeps this at 1 or more. Checked again because % by zero
 * is undefined behaviour in C (it usually ends the program), and this is display
 * code that a test or a future caller could hand anything. */
static uint64_t safe_length(int ticks_per_year)
{
    return ticks_per_year >= 1 ? (uint64_t)ticks_per_year : 1u;
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
    static const char *const NAMES[] = {"spring", "summer", "autumn", "winter"};
    uint64_t length = safe_length(ticks_per_year);
    /* left is below length, so left * 4 / length is always 0 to 3, a safe index,
     * and left fits an int, so left * 4 cannot overflow 64 bits. */
    uint64_t left = ticks % length;
    return NAMES[left * 4u / length];
}

int years_whole(uint64_t ticks, int ticks_per_year)
{
    uint64_t whole = ticks / safe_length(ticks_per_year);
    return whole > (uint64_t)INT_MAX ? INT_MAX : (int)whole;
}
