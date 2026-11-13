#include "branch.h"

#include <stdio.h> /* snprintf */

#include "save.h" /* sim_copy */
#include "years.h"

bool branch_make(Branches *b, const Sim *shown, char *err, size_t len)
{
    if (b->split) {
        snprintf(err, len, "already split in two: keep one first");
        return false;
    }
    /* sim_copy fills b->other only on success; on failure there is nothing
     * in it to free and b stays as it was. */
    if (!sim_copy(&b->other, shown, err, len)) {
        return false;
    }
    b->split = true;
    b->shown = 0;
    b->split_at = shown->tick;
    return true;
}

bool branch_caught_up(const Branches *b, const Sim *shown)
{
    if (!b->split) {
        return true;
    }
    return b->other.tick >= shown->tick || sim_is_over(&b->other);
}

bool branch_catch_up(Branches *b, const Sim *shown, uint64_t (*now_ms)(void),
                     uint64_t budget_ms)
{
    uint64_t start = now_ms();
    while (!branch_caught_up(b, shown)) {
        sim_step(&b->other);
        /* Unsigned subtraction: right even if the clock wrapped round. */
        if (now_ms() - start >= budget_ms) {
            break;
        }
    }
    return branch_caught_up(b, shown);
}

void branch_catch_up_all(Branches *b, const Sim *shown)
{
    while (!branch_caught_up(b, shown)) {
        sim_step(&b->other);
    }
}

bool branch_switch(Branches *b, Sim *shown)
{
    if (!b->split || !branch_caught_up(b, shown)) {
        return false;
    }
    /* A struct assignment copies every byte of the struct, so this is three
     * whole-Sim copies through a temporary. The heap blocks the two Sims point
     * to (cells, creatures, brains) do not move: only the pointers to them
     * trade places, so each block still has exactly one owner. That is safe
     * because no Sim keeps a pointer into its own struct, only into the heap. */
    Sim tmp = *shown;
    *shown = b->other;
    b->other = tmp;
    b->shown = 1 - b->shown;
    return true;
}

void branch_keep_shown(Branches *b)
{
    branch_free(b);
}

void branch_status_text(const Branches *b, const Sim *shown, char *out,
                        size_t size)
{
    if (!b->split) {
        out[0] = '\0';
        return;
    }
    char me = b->shown == 0 ? 'A' : 'B';
    char them = b->shown == 0 ? 'B' : 'A';
    /* Counted as the title bar counts them, from year 0. */
    int year = years_whole(b->split_at, shown->set.ticks_per_year);
    if (sim_is_over(&b->other)) {
        snprintf(out, size, "branch %c, split at year %d | %c: ended", me, year,
                 them);
    } else if (!branch_caught_up(b, shown)) {
        snprintf(out, size, "branch %c, split at year %d | %c: catching up", me,
                 year, them);
    } else {
        snprintf(out, size, "branch %c, split at year %d | %c: %d alive", me,
                 year, them, b->other.pool.count);
    }
}

void branch_free(Branches *b)
{
    if (b->split) {
        sim_free(&b->other);
    }
    *b = (Branches){0};
}
