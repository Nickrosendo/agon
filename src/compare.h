/*
 * compare: two branches of one world side by side (branch.h), as lines of
 * text the window draws: the counts in each, what became of the traits given
 * in either, and charts of the recent past drawn to one scale, so a line that
 * stands higher really is more. No drawing here, so a test can read it.
 */
#ifndef AGON_COMPARE_H
#define AGON_COMPARE_H

#include <stdint.h>

#include "sim.h"

/* Room for each line in bytes (a chart block is 3 bytes of UTF-8 and one
 * column on screen), the most lines, and the widest chart in columns. */
enum { COMPARE_COLS = 192, COMPARE_LINES = 20, COMPARE_CHART_COLS = 32 };

/*
 * Fills lines with the comparison of world a, named name_a ('A' or 'B'), and
 * world b, named name_b, which split at tick split_at, and returns how many
 * lines it used (at most max). a is the one on screen. When b is behind a
 * (it is still catching up), its numbers are from its own tick, and the
 * heading says so.
 */
int compare_lines(const Sim *a, char name_a, const Sim *b, char name_b,
                  uint64_t split_at, char lines[][COMPARE_COLS], int max);

#endif
