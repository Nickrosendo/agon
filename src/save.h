/*
 * save: a whole world to a file and back. The format is written value by
 * value in a fixed byte order, so a file does not depend on this
 * compiler's struct layout or this machine's byte order. A file is not
 * trusted when it is read: every value that could index an array is
 * checked, so a damaged file is refused rather than obeyed.
 */
#ifndef AGON_SAVE_H
#define AGON_SAVE_H

#include <stdbool.h>
#include <stddef.h> /* size_t */

#include "sim.h"

/* Writes the world to path, replacing what is there. The file appears
 * under that name only once it is complete: a failure (or a crash) midway
 * leaves any earlier save untouched. False with a one-line message. */
bool save_write(const Sim *s, const char *path, char *err, size_t errlen);

/* Builds *s from a file: its world, its settings, its seed. *s needs no
 * sim_init first, and on success must later go to sim_free. False with a
 * one-line message; there is then nothing to free. */
bool save_read(Sim *s, const char *path, char *err, size_t errlen);

/* The same bytes a file would hold, in a block of memory on the heap: *bytes
 * and *len on success, and the caller frees *bytes with free. False with a
 * one-line message when memory runs out; there is then nothing to free. */
bool save_to_memory(const Sim *s, unsigned char **bytes, size_t *len,
                    char *err, size_t errlen);
/* save_read from len bytes at bytes instead of a file; the bytes are only
 * read, and can be freed as soon as this returns. */
bool save_from_memory(Sim *s, const unsigned char *bytes, size_t len,
                      char *err, size_t errlen);
/* *to becomes a world exactly like *from, through the save format (so a copy
 * can never quietly differ from a save). *to needs no sim_init first and on
 * success goes to sim_free later; on failure there is nothing to free. */
bool sim_copy(Sim *to, const Sim *from, char *err, size_t errlen);

#endif
