/*
 * years: the calendar. A year is ticks_per_year ticks, in four seasons, spring
 * first, and it is a rule: the season every rule reads comes from here, so what
 * the title says is what the world does. The same functions also turn ticks
 * into the years people read.
 */
#ifndef AGON_YEARS_H
#define AGON_YEARS_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

/* The seasons, in the order a year passes through them. SEASON_COUNT is how many
 * there are, the usual trick of a last enum value. */
enum { SEASON_SPRING, SEASON_SUMMER, SEASON_AUTUMN, SEASON_WINTER, SEASON_COUNT };

/* The shortest year the calendar works with: one tick per season. Shorter or
 * nonsense lengths (0, negative) are read as this inside every function below,
 * so none of them can divide by zero. The settings table refuses them anyway. */
#define YEARS_MIN_LENGTH 4

/* The season these ticks fall in, a SEASON_ value. Tick 0 of every year is
 * spring. When the year does not split into four equal parts, the seasons
 * differ by at most a tick. */
int years_season_of(uint64_t ticks, int ticks_per_year);

/* Where in its year a tick falls: 0 at the first tick of a year, rising towards
 * 1 at the last, and never reaching it. */
float years_phase(uint64_t ticks, int ticks_per_year);

/* True on the first tick of the given season, in any year. */
bool years_season_starts(uint64_t ticks, int ticks_per_year, int season);

/* The shortest day: a day of 1 tick would be noon for ever. Shorter or nonsense
 * lengths are read as this inside the day functions, as for the year. */
#define YEARS_MIN_DAY 2

/* Where in its day a tick falls: 0 at noon, the first tick of every day, rising
 * towards 1 and never reaching it. */
float years_day_phase(uint64_t ticks, int ticks_per_day);

/* How light it is: 1 at noon, 0 at midnight half a day later, along a smooth
 * curve, 0.5 + 0.5 cos(2 pi * phase). One light for the whole map. Below 0.25 it
 * is dark, which is a third of every day. */
float years_light(uint64_t ticks, int ticks_per_day);

/* Writes a length of time as "3 y", or "3.4 y" when a tenth of a year or more
 * is left over. Tenths are cut, never rounded up, so nobody reads as a year old
 * before they are. Always ends the text with '\0' and stays within len. */
void years_format(char *out, size_t len, uint64_t ticks, int ticks_per_year);

/* Whole years gone, for a date: the largest int when it would not fit. */
int years_whole(uint64_t ticks, int ticks_per_year);

/* The name of the season these ticks fall in: "spring", "summer", "autumn" or
 * "winter". The text is a constant the caller must not change or free. */
const char *years_season(uint64_t ticks, int ticks_per_year);

#endif
