/*
 * keyhelp: the window's key list, the page the ? key opens: every key by what
 * it is for, in two columns. Plain text, no drawing, so a test can check it
 * without a window. The window draws the lines (window_main.c, draw_keys).
 */
#ifndef AGON_KEYHELP_H
#define AGON_KEYHELP_H

#include <stdbool.h>

/* What the list is for, as bits to combine with |: in Sandbox mode, in the
 * browser demo (fewer tools), in a browser page (no saving, no quitting). */
enum {
    KEYHELP_SANDBOX = 1u << 0,
    KEYHELP_DEMO = 1u << 1,
    KEYHELP_PAGE = 1u << 2,
};

/* The most lines a column has, and the widest a line is, in characters. */
#define KEYHELP_MAX_LINES 24
#define KEYHELP_WIDTH 62

/* Puts column 0 (watching and following) or column 1 (modes, codes and the
 * Sandbox tools) in lines, at most max of them, and returns how many. The
 * strings are fixed text the caller must not free or change. */
int keyhelp_column(unsigned flags, int column, const char **lines, int max);

#endif
