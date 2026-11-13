/*
 * branch: one world split into two from this moment, to try something in one
 * and watch both (the player agency design, 6.5). The second world is a copy
 * made through the save format (sim_copy), so the two are the same world
 * until the player does something in one of them.
 *
 * The world on screen is always the caller's own Sim; the Branches hold the
 * other one. Switching swaps the two whole structs, so everything that already
 * points at the caller's Sim (the scene, the panel, the inspector) goes on
 * pointing at the world on screen without being told.
 */
#ifndef AGON_BRANCH_H
#define AGON_BRANCH_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "sim.h"

typedef struct {
    bool split;       /* two worlds; when false, other holds nothing */
    Sim other;        /* the world not on screen, owned by the Branches */
    int shown;        /* which one is on screen: 0 for A, the first, 1 for B */
    uint64_t split_at; /* the tick the two were the same */
} Branches;

/* Copies *shown into the other branch. False with a one-line message when the
 * world is already split or memory runs out; nothing changes then. */
bool branch_make(Branches *b, const Sim *shown, char *err, size_t len);
/* The other branch has reached the shown world's tick, or its own world has
 * ended (a dead world has nothing left to catch up). True when not split. */
bool branch_caught_up(const Branches *b, const Sim *shown);
/* Steps the other branch towards the shown world's tick until budget_ms by
 * now_ms has passed. Returns whether it has caught up. */
bool branch_catch_up(Branches *b, const Sim *shown, uint64_t (*now_ms)(void),
                     uint64_t budget_ms);
/* The same with no clock: for tests and runs that must not depend on time. */
void branch_catch_up_all(Branches *b, const Sim *shown);
/* Puts the other world on screen: *shown and b->other trade places. False
 * (and nothing moves) when not split or the other is still catching up. */
bool branch_switch(Branches *b, Sim *shown);
/* Keeps the world on screen and frees the other. Nothing when not split. */
void branch_keep_shown(Branches *b);
/* "branch A of A/B, split at year 3 | B: 120 alive" for the status bar; an
 * empty string when not split. */
void branch_status_text(const Branches *b, const Sim *shown, char *out,
                        size_t size);
/* Frees the other world, if any. The Branches can be split again afterwards. */
void branch_free(Branches *b);

#endif
