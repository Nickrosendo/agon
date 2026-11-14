/*
 * worldcode: a world as a line of text someone can copy and paste (the player
 * agency design, section 7). A code holds what is needed to grow the world
 * again, not the world itself: the rules version, the seed, the size, the
 * settings that differ from the defaults, the player's action log and the
 * tick the world had reached. Opening a code builds the world at tick 0 and
 * replays it up to that tick; the same seed, settings and actions give the
 * same world, which tests/test_worldcode.c checks.
 *
 * The text is "agon1:" and then the bytes below in base64 (64 safe characters,
 * six bits each; see worldcode.c):
 *   u32 length of what follows, up to the check;
 *   u16 rules version (AGON_RULES), u64 seed, u16 width, u16 height,
 *   u64 the tick reached;
 *   u16 how many settings differ, then for each u16 its row in the settings
 *   table, rows in rising order, and its value: an int or a float as 4 bytes,
 *   a text as a u8 length and its characters;
 *   u32 how many actions, then each as in a save file;
 *   u32 the check: a fingerprint of every byte before it.
 * The length and the check are what tell a cut code from one with a typo.
 */
#ifndef AGON_WORLDCODE_H
#define AGON_WORLDCODE_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "actions.h"
#include "settings.h"
#include "sim.h"

/* What every code starts with. The 1 is the layout of the bytes, which is not
 * the rules version: the rules can change while the layout stays. */
#define WORLDCODE_PREFIX "agon1:"

/* In a link to the browser page, the code is this field of the address:
 * ".../agon/?code=agon1:...". */
#define WORLDCODE_LINK_FIELD "code="

/* What a code holds, read back. log owns its memory: worldcode_free. */
typedef struct {
    Settings set;
    uint64_t seed;
    int w, h;
    uint64_t until; /* the tick the world had reached when the code was made */
    ActionLog log;
} WorldCode;

/* The code for *s as a string on the heap, '\0' at the end, in *text; the
 * caller frees it with free. False when memory runs out, with nothing to
 * free. */
bool worldcode_write(const Sim *s, char **text);
/* The same, claiming another rules version: only for tests, which need a
 * sound code from other rules to see it refused. */
bool worldcode_write_as(const Sim *s, unsigned rules, char **text);

/* Reads a code, alone or inside a link to the browser page (the address's
 * "code=" field). Spaces and line breaks anywhere are skipped, since a pasted
 * code may have been wrapped. False with one plain line in err, and nothing
 * allocated, for a text that is not a code, a typo, a code cut short, a code
 * from other rules, or values outside what this agon allows. */
bool worldcode_read(const char *text, WorldCode *code, char *err, size_t len);

void worldcode_free(WorldCode *code);

/* Builds the code's world at tick 0, with its whole action log, ready to be
 * stepped up to code->until. *s needs no sim_init first and on success goes to
 * sim_free later. False with a message when the world cannot be built (out of
 * memory, say); nothing to free then. */
bool worldcode_start(Sim *s, const WorldCode *code, char *err, size_t len);

#endif
