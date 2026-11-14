#include "test.h"

#include <string.h> /* strstr, strcmp, strlen */

#include "actions.h"
#include "compare.h"
#include "genome.h"
#include "save.h" /* sim_copy */
#include "settings.h"
#include "sim.h"

static int first_alive(const Sim *s)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            return i;
        }
    }
    return -1;
}

/* A world 300 ticks old and an exact copy of it, as x makes. */
static void two_equal(Sim *a, Sim *b)
{
    Settings set = settings_default();
    CHECK(sim_init(a, &set, 17, 64, 32));
    for (int i = 0; i < 300; i++) {
        sim_step(a);
    }
    char err[128];
    CHECK(sim_copy(b, a, err, sizeof(err)));
}

static bool some_line_has(char lines[][COMPARE_COLS], int n, const char *a,
                          const char *b)
{
    for (int i = 0; i < n; i++) {
        if (strstr(lines[i], a) != NULL && (b == NULL || strstr(lines[i], b) != NULL)) {
            return true;
        }
    }
    return false;
}

/* Two equal worlds: every count is the same, and the two chart lines of each
 * pair are drawn alike (the same blocks after their labels). */
static void test_equal_worlds_read_same(void)
{
    Sim a, b;
    two_equal(&a, &b);
    static char lines[COMPARE_LINES][COMPARE_COLS];
    int n = compare_lines(&a, 'A', &b, 'B', 100, lines, COMPARE_LINES);
    CHECK(n > 10 && n <= COMPARE_LINES);
    CHECK(some_line_has(lines, n, "creatures", "same"));
    CHECK(some_line_has(lines, n, "plant-eaters", "same"));
    CHECK(!some_line_has(lines, n, "catching up", NULL));
    for (int i = 0; i + 1 < n; i++) {
        /* A chart pair: "  plant-eaters     A " then blocks, and the same
         * label with B on the next line. */
        if (strstr(lines[i], " A \xE2") != NULL) {
            const char *blocks_a = strstr(lines[i], " A ") + 3;
            const char *blocks_b = strstr(lines[i + 1], " B ") + 3;
            CHECK(strncmp(blocks_a, blocks_b, strlen(blocks_a)) == 0);
        }
    }
    for (int i = 0; i < n; i++) {
        CHECK(strlen(lines[i]) < COMPARE_COLS);
    }
    sim_free(&a);
    sim_free(&b);
}

/* A trait given in B only gets a row: carried in B, by nobody in A. And a
 * world behind the shown one is said to be catching up. */
static void test_gift_and_catching_up(void)
{
    Sim a, b;
    two_equal(&a, &b);
    int slot = first_alive(&b);
    CHECK(slot >= 0);
    /* Some bodies carry armour near 1 already (genes pile up at their ends),
     * so B has exactly one carrier more than A, not one against none. Taken
     * from a creature that does not carry it yet. */
    while (slot < b.pool.capacity &&
           (!b.pool.items[slot].alive ||
            b.pool.items[slot].genome.g[GENE_ARMOUR] > 0.9f)) {
        slot++;
    }
    CHECK(slot < b.pool.capacity);
    CHECK(actions_give_trait(&b, b.pool.items[slot].id, GENE_ARMOUR, 1.0f));
    sim_step(&b);
    sim_step(&a);
    sim_step(&a); /* a is now one tick ahead */
    static char lines[COMPARE_LINES][COMPARE_COLS];
    int n = compare_lines(&a, 'A', &b, 'B', 300, lines, COMPARE_LINES);
    int in_a = actions_carriers(&a, GENE_ARMOUR, 1.0f);
    int in_b = actions_carriers(&b, GENE_ARMOUR, 1.0f);
    CHECK(in_b == in_a + 1);
    char want[64];
    snprintf(want, sizeof(want), "%8d %8d", in_a, in_b);
    CHECK(some_line_has(lines, n, "armour 1.00 given", want));
    CHECK(some_line_has(lines, n, "B is still catching up", NULL));
    sim_free(&a);
    sim_free(&b);
}

/* Too little room for the table: nothing rather than half of it. */
static void test_no_room_no_lines(void)
{
    Sim a, b;
    two_equal(&a, &b);
    char lines[4][COMPARE_COLS];
    CHECK(compare_lines(&a, 'A', &b, 'B', 0, lines, 4) == 0);
    sim_free(&a);
    sim_free(&b);
}

int main(void)
{
    test_equal_worlds_read_same();
    test_gift_and_catching_up();
    test_no_room_no_lines();
    TEST_SUMMARY();
}
