/*
 * years: time as people read it. Ticks still drive the simulation; a year is
 * only a way of showing them, and no rule ever reads ticks_per_year, so the
 * setting cannot change how a world evolves.
 */
#ifndef AGON_YEARS_H
#define AGON_YEARS_H

#include <stddef.h> /* size_t */
#include <stdint.h>

/* Writes a length of time as "3 y", or "3.4 y" when a tenth of a year or more
 * is left over. Tenths are cut, never rounded up, so nobody reads as a year old
 * before they are. Always ends the text with '\0' and stays within len. */
void years_format(char *out, size_t len, uint64_t ticks, int ticks_per_year);

/* Whole years gone, for a date: the largest int when it would not fit. */
int years_whole(uint64_t ticks, int ticks_per_year);

#endif
