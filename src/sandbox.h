/*
 * sandbox: the window's two modes and the Sandbox tools' menus, as plain
 * state and text, with no drawing, so they can be tested without a window
 * (the player agency design, sections 5 and 6). The mode belongs to the screen,
 * not to the world: nothing here is saved, and switching modes changes no world.
 * What changes a world goes through the action log (actions.h).
 */
#ifndef AGON_SANDBOX_H
#define AGON_SANDBOX_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "sim.h"
#include "skip.h"

enum {
    SANDBOX_MENU_NONE,
    SANDBOX_MENU_TRAIT,   /* g: give the followed creature a trait */
    SANDBOX_MENU_WEATHER, /* w: start a weather event */
    SANDBOX_MENU_SKIP,    /* k: how far to skip */
    SANDBOX_MENU_SUMMARY, /* after a skip: while you were away */
    SANDBOX_MENU_SETUP    /* u, or at the start: how bodies change in a new world */
};

/* What a key did: nothing (the window handles it as before), taken, or taken
 * and the setup screen asks for the world to start again with its settings
 * (sandbox_setup_apply), which only the window can do. */
enum { SANDBOX_IGNORED, SANDBOX_TAKEN, SANDBOX_RESTART };

/* The steps the setup screen offers: how many times a body trait's usual
 * change (trait_mutation_scale), and the chance a change is a jump. */
enum { SANDBOX_WILD_STEPS = 6, SANDBOX_JUMP_STEPS = 5 };

/* The step of the trait menu's value: , and . move it by this. */
#define SANDBOX_VALUE_STEP 0.05f

typedef struct {
    bool on;          /* Sandbox mode; false is Documentary */
    int menu;         /* a SANDBOX_MENU_ value */
    int trait;        /* the trait menu's choice: an index into ACTIONS_GIVABLE */
    float value;      /* and its value, 0..1 */
    uint32_t target;  /* the creature the trait menu is for */
    Skip skip;        /* a skip under way, or the last one's summary */
    uint64_t skip_started_ms;
    /* The setup screen's choices, as steps into the tables in sandbox.c, and
     * the genes that may jump, as bits of jump_genes (settings.h). */
    int wild_step, jump_step;
    int jump_genes;
} Sandbox;

/* Opens the setup screen at the values set has now (the nearest steps). */
void sandbox_open_setup(Sandbox *b, const Settings *set);
/* Writes the setup screen's choices into set: trait_mutation_scale,
 * jump_chance and jump_genes. */
void sandbox_setup_apply(const Sandbox *b, Settings *set);

/* Handles a key: m switches modes in either; the rest only in Sandbox. The
 * browser demo (built with AGON_DEMO) has only the weather of the tools. now_ms
 * is the clock's reading, kept as the start of a skip. followed is the creature
 * the camera follows, or NULL. A line for the player goes to notice. */
int sandbox_key(Sandbox *b, Sim *s, int key, const Creature *followed,
                uint64_t now_ms, char *notice, size_t size);
/* Runs a skip under way for budget_ms; when it ends, the summary opens.
 * Returns whether a skip is under way. */
bool sandbox_skip_step(Sandbox *b, Sim *s, uint64_t (*now_ms)(void),
                       uint64_t budget_ms);
bool sandbox_skipping(const Sandbox *b);
/* The lines of the open menu, or of the skip's progress; 0 when nothing is
 * open. */
int sandbox_menu_lines(const Sandbox *b, const Sim *s, uint64_t now_ms,
                       char lines[][SKIP_COLS], int max);
/* The keys line for the bottom of the window. */
const char *sandbox_help(const Sandbox *b);
void sandbox_free(Sandbox *b);

#endif
