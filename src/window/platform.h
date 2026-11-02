/*
 * platform: the window, its input and its clock. The rest of the window
 * program sees only this header, and platform_sdl.c is the only file that
 * includes SDL, so another library or system would mean replacing one file.
 */
#ifndef AGON_PLATFORM_H
#define AGON_PLATFORM_H

#include <stdbool.h>
#include <stddef.h> /* size_t */
#include <stdint.h>

#include "canvas.h"

typedef enum { EV_QUIT, EV_KEY, EV_WHEEL, EV_DOWN, EV_UP, EV_MOVE, EV_RESIZE }
    EventKind;

/* Not every field means something for every kind: key for EV_KEY, x and y
 * for the mouse events, delta for EV_WHEEL (positive is away from you),
 * w and h for EV_RESIZE. */
typedef struct {
    EventKind kind;
    int key, x, y, delta, w, h;
} Event;

/* Keys without a character of their own. Others use their ASCII code. */
enum { KEY_UP = 1000, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_ESCAPE };

/* Opens a resizable window. False with a message in err. */
bool platform_open(const char *title, int w, int h, char *err, size_t errlen);
void platform_close(void);
/* The next waiting event, into *e; false when there are none. */
bool platform_poll(Event *e);
/* Shows the canvas. An empty canvas shows nothing. */
void platform_present(const Canvas *c);
/* Tells the user about a failure: a message box on Windows, where a window
 * program has no console, and standard error elsewhere. */
void platform_error(const char *message);
/* Milliseconds from a fixed but arbitrary starting point. */
uint64_t platform_now_ms(void);
/*
 * Calls frame(arg) over and over, frames_per_second times a second, until
 * platform_stop; with 0, as fast as it can. The only loop in the program,
 * because a browser insists on owning the loop itself: there, this function
 * will hand frame to the browser. frame is a pointer to a function, and arg
 * a pointer to anything: void * says only "an address", and frame casts it
 * back to what it really is.
 */
void platform_run(void (*frame)(void *), void *arg, int frames_per_second);
void platform_stop(void);
/* Puts an EV_QUIT in the queue, exactly what the window's X button does. */
void platform_post_quit(void);

#endif
