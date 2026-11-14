#include "playlog.h"

#include <errno.h>  /* errno */
#include <stdio.h>  /* fopen, fprintf, fclose, snprintf */
#include <string.h> /* strerror */

void playlog_init(Playlog *p)
{
    *p = (Playlog){.first_sandbox_ms = -1};
}

void playlog_frame(Playlog *p, uint64_t now_ms, bool sandbox)
{
    if (!p->started) {
        p->started = true;
        p->start_ms = now_ms;
        p->last_ms = now_ms;
        p->sandbox = sandbox;
    }
    /* A clock that went back (it should not) counts as no time, rather than
     * as an unsigned wrap round to billions of milliseconds. */
    uint64_t step = now_ms > p->last_ms ? now_ms - p->last_ms : 0;
    /* The time since the last frame was spent in the mode it was then. A
     * bool used as an index is 0 or 1. */
    p->ms_in[p->sandbox] += step;
    if (sandbox && p->first_sandbox_ms < 0) {
        p->first_sandbox_ms = (int64_t)(now_ms - p->start_ms);
    }
    p->sandbox = sandbox;
    p->last_ms = now_ms;
}

void playlog_key(Playlog *p, int key)
{
    if (key > 0 && key < PLAYLOG_KEYS) {
        p->keys[key]++;
    } else {
        p->other_keys++;
    }
}

void playlog_click(Playlog *p)
{
    p->clicks++;
}

/* A key's name in the file: the character itself, or a word for a key that
 * is blank or would make the line hard to read. */
static void key_name(int key, char *out, size_t size)
{
    switch (key) {
    case ' ': snprintf(out, size, "space"); break;
    case '\t': snprintf(out, size, "tab"); break;
    case '\r': snprintf(out, size, "enter"); break;
    case '\b': snprintf(out, size, "backspace"); break;
    default:
        if (key > ' ' && key < 127) {
            snprintf(out, size, "%c", key);
        } else {
            snprintf(out, size, "code%d", key);
        }
    }
}

bool playlog_write(const Playlog *p, const char *path, char *err, size_t len)
{
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        /* errno: the reason fopen failed, kept by the C library in a global
         * number; strerror turns it into words. Read at once, before any other
         * call can change it. */
        snprintf(err, len, "cannot write the playlog %s: %s", path, strerror(errno));
        return false;
    }
    uint64_t total = p->last_ms - p->start_ms;
    fprintf(f, "agon-playlog 1\n");
    /* Seconds, whole: %llu with a cast, since uint64_t has no letter of its
     * own in printf (inttypes.h has PRIu64 for it; the cast is simpler). */
    fprintf(f, "seconds %llu\n", (unsigned long long)(total / 1000u));
    fprintf(f, "seconds_documentary %llu\n", (unsigned long long)(p->ms_in[0] / 1000u));
    fprintf(f, "seconds_sandbox %llu\n", (unsigned long long)(p->ms_in[1] / 1000u));
    fprintf(f, "first_sandbox_second %lld\n",
            p->first_sandbox_ms < 0 ? -1LL : (long long)(p->first_sandbox_ms / 1000));
    fprintf(f, "clicks %u\n", (unsigned)p->clicks);
    for (int k = 0; k < PLAYLOG_KEYS; k++) {
        if (p->keys[k] > 0) {
            char name[16];
            key_name(k, name, sizeof(name));
            fprintf(f, "key %s %u\n", name, (unsigned)p->keys[k]);
        }
    }
    fprintf(f, "key other %u\n", (unsigned)p->other_keys);
    /* fclose writes what the library still holds in its buffer; a full disk
     * shows up here, not at fprintf. */
    if (fclose(f) != 0) {
        snprintf(err, len, "cannot finish the playlog %s", path);
        return false;
    }
    return true;
}
