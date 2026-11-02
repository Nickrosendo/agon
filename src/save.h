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

#endif
