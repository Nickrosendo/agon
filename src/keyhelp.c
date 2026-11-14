#include "keyhelp.h"

/* Adds one line if there is room. lines is the caller's array; *n counts what
 * is in it. */
static void add(const char **lines, int max, int *n, const char *line)
{
    if (*n < max) {
        lines[*n] = line;
        (*n)++;
    }
}

/* Watching and following: the same in both modes. */
static int watching(unsigned flags, const char **lines, int max)
{
    int n = 0;
    add(lines, max, &n, "Watching");
    add(lines, max, &n, "  space      pause, and go on");
    add(lines, max, &n, "  + -        faster, slower");
    add(lines, max, &n, "  [ ] wheel  zoom out, in");
    add(lines, max, &n, "  arrows     move the view (or drag it)");
    add(lines, max, &n, "  f          the camera: follows whoever has the most");
    add(lines, max, &n, "             coming this season (on in Documentary)");
    add(lines, max, &n, "  r          a new world (n, after one has died out)");
    add(lines, max, &n, "");
    add(lines, max, &n, "Following");
    add(lines, max, &n, "  click      follow a creature; bare ground lets go");
    add(lines, max, &n, "  Esc        let go, or close a page");
    add(lines, max, &n, "  l          the life story of the one followed");
    add(lines, max, &n, "  e          notices, rare things as they happen;");
    add(lines, max, &n, "             1-9 or a click follows who one is about");
    add(lines, max, &n, "  j          follow who the newest notice is about");
    add(lines, max, &n, "");
    add(lines, max, &n, "Pages");
    add(lines, max, &n, "  b          the species lab (1-6 actions, p parts)");
    add(lines, max, &n, "  a          animals drawn from frames, or skeletons");
    add(lines, max, &n, "  ? or h     this list (Esc closes it too)");
    /* A page cannot write files or close its own tab. */
    if ((flags & KEYHELP_PAGE) == 0) {
        add(lines, max, &n, "  s  q       save, quit");
    }
    return n;
}

/* The modes, world codes and Sandbox's tools. */
static int changing(unsigned flags, const char **lines, int max)
{
    int n = 0;
    add(lines, max, &n, "Modes");
    add(lines, max, &n, "  m          switch: Documentary (watch) or Sandbox");
    add(lines, max, &n, (flags & KEYHELP_SANDBOX) != 0
                            ? "             (change the world). Now: Sandbox"
                            : "             (change the world). Now: Documentary");
    add(lines, max, &n, "");
    add(lines, max, &n, "World codes");
    add(lines, max, &n, (flags & KEYHELP_PAGE) != 0
                            ? "  c          copy a link to this world: whoever opens"
                            : "  c          copy this world's code: whoever opens");
    add(lines, max, &n, "             it sees it again, with all you did to it");
    add(lines, max, &n, "  o          open a code (or a link holding one)");
    add(lines, max, &n, "");
    add(lines, max, &n, (flags & KEYHELP_SANDBOX) != 0 ? "Sandbox tools"
                                                        : "Sandbox tools (m first)");
    add(lines, max, &n, "  w          weather for a season: drought, hard");
    add(lines, max, &n, "             winter or a warm year");
    if ((flags & KEYHELP_DEMO) != 0) {
        add(lines, max, &n, "");
        add(lines, max, &n, "  The full game also gives traits, skips years");
        add(lines, max, &n, "  ahead, splits a world to compare, and starts");
        add(lines, max, &n, "  worlds with other rules for how bodies change.");
        return n;
    }
    add(lines, max, &n, "  g          give the one followed a trait");
    add(lines, max, &n, "  k          skip 1, 5 or 20 years ahead, then see");
    add(lines, max, &n, "             what happened (Esc stops)");
    add(lines, max, &n, "  x          split the world in two to compare;");
    add(lines, max, &n, "             x twice keeps the half shown");
    add(lines, max, &n, "  Tab        show the other half");
    add(lines, max, &n, "  v          the two halves side by side");
    add(lines, max, &n, "  u          a new start: how wildly bodies change");
    return n;
}

int keyhelp_column(unsigned flags, int column, const char **lines, int max)
{
    return column == 0 ? watching(flags, lines, max) : changing(flags, lines, max);
}
