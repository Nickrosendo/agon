/*
 * playlog: a record of one sitting in the window, for playtests: how long it
 * lasted, how long in each mode, how soon Sandbox was first opened, and how
 * often each key and click was used. Written to a plain text file on leaving
 * (--playlog PATH), one "name value" pair a line, so a few players' files can
 * be read side by side or added up with a short script. Nothing is sent
 * anywhere; nothing about the world is in it.
 */
#ifndef AGON_PLAYLOG_H
#define AGON_PLAYLOG_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

/* Keys with a character (below 128) are counted one by one; arrows, Escape
 * and the rest together as "other". */
enum { PLAYLOG_KEYS = 128 };

typedef struct {
    bool started;
    uint64_t start_ms;   /* the first frame */
    uint64_t last_ms;    /* the frame before this one */
    bool sandbox;        /* the mode at the last frame */
    uint64_t ms_in[2];   /* time in Documentary [0] and Sandbox [1] */
    int64_t first_sandbox_ms; /* after the start; -1 while never */
    uint32_t keys[PLAYLOG_KEYS];
    uint32_t other_keys;
    uint32_t clicks;
} Playlog;

/* Empty: nothing counted yet. */
void playlog_init(Playlog *p);
/* Called once a frame with the clock and the mode then: the time since the
 * last frame goes to the mode the window was in. */
void playlog_frame(Playlog *p, uint64_t now_ms, bool sandbox);
/* One key pressed (a KEY_ value or a character), one click on the world. */
void playlog_key(Playlog *p, int key);
void playlog_click(Playlog *p);
/* Writes the record to path, replacing the file. False with one line in err
 * when the file cannot be written. */
bool playlog_write(const Playlog *p, const char *path, char *err, size_t len);

#endif
