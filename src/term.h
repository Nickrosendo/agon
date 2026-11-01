/*
 * term: the terminal layer. Raw keyboard input, full-screen colour output
 * built into one buffer per frame, window size, and putting the terminal
 * back the way it was however the program ends.
 *
 * It knows nothing about the simulation. Coordinates are 0-based: column 0,
 * row 0 is the top left character cell.
 */

/* Include guard: if this header is pasted into the same .c file twice
 * (directly or through another header), the second copy is skipped. */
#ifndef AGON_TERM_H
#define AGON_TERM_H

#include <stdbool.h>
#include <stdint.h>

/* Raw mode, alternate screen, hidden cursor; installs the clean-up for
 * every kind of exit. False if standard input or output is not a terminal
 * or the terminal refuses the settings. */
bool term_open(void);
/* Undoes term_open. Safe to call twice, and from a signal handler. */
void term_close(void);

/* For output without term_open (agon --once): picks 24-bit or 256
 * colours from the environment, as term_open does. A frame can then be
 * built and written with term_begin ... term_end as usual; it goes to
 * standard output whether or not that is a terminal. */
void term_detect_colour(void);
/* Back to the terminal's default colours, as part of this frame. */
void term_reset_colours(void);

void term_size(int *cols, int *rows);
bool term_resized(void); /* true once after each resize */
int term_key(void);      /* the next byte typed, or -1 when none is waiting */
bool term_truecolor(void);

/* A frame: term_begin, any number of moves, colours and puts, then
 * term_end, which writes the whole frame to the terminal at once. */
void term_begin(void);
void term_clear(void); /* blank the whole screen as part of this frame */
void term_move(int col, int row);
void term_fg(uint8_t r, uint8_t g, uint8_t b);
void term_bg(uint8_t r, uint8_t g, uint8_t b);
void term_put(const char *utf8);
void term_end(void);

#endif
