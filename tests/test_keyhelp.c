#include "test.h"

#include <string.h> /* strlen, strncmp */

#include "keyhelp.h"

/* Both columns for flags, one after the other, in all; returns the count. */
static int both(unsigned flags, const char **all, int max)
{
    int n = keyhelp_column(flags, 0, all, max);
    return n + keyhelp_column(flags, 1, all + n, max - n);
}

/* Whether some line starts with "  " and then key and a space: the key's own
 * row, not a mention of it in another's text. */
static bool lists_key(const char **lines, int n, const char *key)
{
    size_t k = strlen(key);
    for (int i = 0; i < n; i++) {
        if (strncmp(lines[i], "  ", 2) == 0 && strncmp(lines[i] + 2, key, k) == 0 &&
            lines[i][2 + k] == ' ') {
            return true;
        }
    }
    return false;
}

/* Every combination fits: no column longer than KEYHELP_MAX_LINES (asking
 * for one more line than that shows nothing was cut), no line wider than
 * KEYHELP_WIDTH. */
static void test_every_list_fits(void)
{
    for (unsigned flags = 0; flags < 8; flags++) {
        for (int column = 0; column < 2; column++) {
            const char *lines[KEYHELP_MAX_LINES + 1];
            int n = keyhelp_column(flags, column, lines, KEYHELP_MAX_LINES + 1);
            CHECK(n > 0 && n <= KEYHELP_MAX_LINES);
            for (int i = 0; i < n; i++) {
                CHECK(strlen(lines[i]) <= KEYHELP_WIDTH);
            }
        }
    }
}

/* The desktop program lists every key it answers to. */
static void test_desktop_lists_every_key(void)
{
    const char *all[2 * KEYHELP_MAX_LINES];
    int n = both(KEYHELP_SANDBOX, all, 2 * KEYHELP_MAX_LINES);
    const char *keys[] = {"space", "+ -", "[ ] wheel", "arrows", "f", "r", "click",
                          "Esc", "l", "e", "j", "b", "a", "? or h", "s  q", "m",
                          "c", "o", "w", "g", "k", "x", "Tab", "v", "u"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (!lists_key(all, n, keys[i])) {
            fprintf(stderr, "  key list misses \"%s\"\n", keys[i]);
            CHECK(false);
        }
    }
}

/* The demo lists only the tools it has, and a page neither saves nor quits. */
static void test_demo_and_page_leave_out(void)
{
    const char *all[2 * KEYHELP_MAX_LINES];
    unsigned flags = KEYHELP_SANDBOX | KEYHELP_DEMO | KEYHELP_PAGE;
    int n = both(flags, all, 2 * KEYHELP_MAX_LINES);
    CHECK(lists_key(all, n, "w"));
    CHECK(!lists_key(all, n, "g"));
    CHECK(!lists_key(all, n, "k"));
    CHECK(!lists_key(all, n, "x"));
    CHECK(!lists_key(all, n, "u"));
    CHECK(!lists_key(all, n, "v"));
    CHECK(!lists_key(all, n, "s  q"));
}

int main(void)
{
    test_every_list_fits();
    test_desktop_lists_every_key();
    test_demo_and_page_leave_out();
    TEST_SUMMARY();
}
