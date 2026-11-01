#include <stdint.h> /* uint8_t: an unsigned integer exactly 8 bits wide */
#include <stdio.h>
#include <stdlib.h> /* malloc, calloc, free */
#include <string.h> /* memcpy, memset, strlen */
#include <time.h>   /* clock_gettime, nanosleep, struct timespec (POSIX) */

#include "term.h" /* quotes: look in this file's folder first */

#define FRAMES_PER_SECOND 30
#define NANOSECONDS_PER_SECOND 1000000000L
#define FRAME_NANOSECONDS (NANOSECONDS_PER_SECOND / FRAMES_PER_SECOND)

/* Smallest window we draw in; below it, a message instead. */
#define MIN_COLS 20
#define MIN_ROWS 6

/* Generations per second for each speed step; + and - move along it. */
static const int SPEEDS[] = {1, 2, 5, 10, 15, 30, 60, 120};
#define SPEED_COUNT ((int)(sizeof(SPEEDS) / sizeof(SPEEDS[0])))
#define DEFAULT_SPEED 3 /* 10 generations per second */

/* The upper half block, U+2580, as its three UTF-8 bytes. The top half of
 * the character cell takes the foreground colour, the bottom half the
 * background colour, so one character shows two square cells. */
#define UPPER_HALF_BLOCK "\xE2\x96\x80"

typedef struct {
    uint8_t r, g, b;
} Colour;

static const Colour ALIVE = {130, 220, 150};
static const Colour DEAD = {14, 17, 23};
static const Colour STATUS_TEXT = {150, 155, 165};

/*
 * The grid is one flat array of w * h cells, row after row.
 * Cell (x, y) lives at y * w + x. The array itself does not know its own
 * size, so every function that walks it is told w and h.
 *
 * Coordinates outside the grid wrap to the other side. In C, % keeps the
 * sign of the left operand: (-1) % 5 is -1, not 4. Adding w and taking
 * % again turns -1 into w - 1. This only works for x >= -w, which is all
 * we need, since neighbours are at most one step away.
 */
static int idx(int x, int y, int w, int h)
{
    int wrapped_x = ((x % w) + w) % w;
    int wrapped_y = ((y % h) + h) % h;
    return wrapped_y * w + wrapped_x;
}

static int count_neighbours(const uint8_t *grid, int x, int y, int w, int h)
{
    int count = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) {
                continue;
            }
            /* Each cell is 0 or 1, so adding them counts the live ones.
             * The uint8_t is widened to int before the addition. */
            count += grid[idx(x + dx, y + dy, w, h)];
        }
    }
    return count;
}

/*
 * Reads every cell of current and writes every cell of next. current is
 * never written here (const promises that, and the compiler enforces it),
 * so every cell sees the same generation.
 */
static void step(const uint8_t *current, uint8_t *next, int w, int h)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int i = idx(x, y, w, h);
            int n = count_neighbours(current, x, y, w, h);
            if (current[i]) {
                next[i] = (n == 2 || n == 3) ? 1 : 0; /* survives */
            } else {
                next[i] = (n == 3) ? 1 : 0; /* born */
            }
        }
    }
}

/*
 * Copies a small pattern into the grid with its top left corner at
 * (x0, y0). '#' is a live cell, anything else is left as it is. The
 * pattern is an array of strings, one per row; its length is passed in
 * because the function only receives a pointer to the first row.
 */
static void place_pattern(uint8_t *grid, int w, int h, int x0, int y0,
                          const char *const *rows, int row_count)
{
    for (int dy = 0; dy < row_count; dy++) {
        const char *row = rows[dy];
        /* A C string ends at the first '\0' byte; that is how the loop
         * knows where the row stops. */
        for (int dx = 0; row[dx] != '\0'; dx++) {
            if (row[dx] == '#') {
                grid[idx(x0 + dx, y0 + dy, w, h)] = 1;
            }
        }
    }
}

static void seed(uint8_t *grid, int w, int h)
{
    /* The acorn: seven cells that keep changing for about 5,000
     * generations on an open plane. */
    static const char *const ACORN[] = {
        ".#.....",
        "...#...",
        "##..###",
    };
    /* The R-pentomino: five cells, about 1,100 generations of chaos. */
    static const char *const R_PENTOMINO[] = {
        ".##",
        "##.",
        ".#.",
    };
    place_pattern(grid, w, h, w / 2 - 3, h / 2 - 1, ACORN, 3);
    place_pattern(grid, w, h, w / 5, h / 4, R_PENTOMINO, 3);
}

static void set_fg(Colour c)
{
    term_fg(c.r, c.g, c.b);
}

static void set_bg(Colour c)
{
    term_bg(c.r, c.g, c.b);
}

/*
 * Writes text at the current position, cut to max_cols characters and
 * padded with spaces to exactly max_cols, so a shorter text fully covers
 * a longer one from the frame before. Only for ASCII text: there one
 * byte is one column on screen.
 */
static void put_padded(const char *text, int max_cols)
{
    char line[512];
    size_t width = (size_t)max_cols;
    if (width > sizeof(line) - 1) {
        width = sizeof(line) - 1;
    }
    size_t len = strlen(text);
    if (len > width) {
        len = width;
    }
    memcpy(line, text, len);
    memset(line + len, ' ', width - len);
    line[width] = '\0';
    term_put(line);
}

/*
 * Each character cell shows two grid cells: (x, 2*row) on top and
 * (x, 2*row + 1) below it. Draws as much of the grid as fits; if the
 * window is larger than the grid, the rest stays blank.
 */
static void draw_grid(const uint8_t *grid, int w, int h, int cols, int rows)
{
    int view_cols = cols < w ? cols : w;
    int view_rows = rows < h / 2 ? rows : h / 2;
    for (int row = 0; row < view_rows; row++) {
        term_move(0, row);
        for (int x = 0; x < view_cols; x++) {
            uint8_t top = grid[idx(x, 2 * row, w, h)];
            uint8_t bottom = grid[idx(x, 2 * row + 1, w, h)];
            if (top == bottom) {
                /* Both halves the same: a space in the background colour
                 * looks identical and is 1 byte instead of 3. Most of the
                 * screen is dead cells, so this shrinks the frame a lot. */
                set_bg(top ? ALIVE : DEAD);
                term_put(" ");
            } else {
                set_fg(top ? ALIVE : DEAD);
                set_bg(bottom ? ALIVE : DEAD);
                term_put(UPPER_HALF_BLOCK);
            }
        }
    }
}

static void draw_status(int row, int cols, long generation, int speed,
                        int paused)
{
    char text[160];
    snprintf(text, sizeof(text),
             " generation %ld | %d/s%s | space pause | +/- speed | q quit",
             generation, SPEEDS[speed], paused ? " paused" : "");
    term_move(0, row);
    set_fg(STATUS_TEXT);
    set_bg(DEAD);
    put_padded(text, cols);
}

static void draw_too_small(int cols, int rows)
{
    set_fg(STATUS_TEXT);
    set_bg(DEAD);
    term_move(0, 0);
    put_padded("window too small", cols);
    if (rows > 1) {
        term_move(0, 1);
        put_padded("need 20 x 6", cols);
    }
}

/* Nanoseconds from a fixed but arbitrary starting point. CLOCK_MONOTONIC
 * never jumps back when the system clock is changed. */
static int64_t now_ns(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * NANOSECONDS_PER_SECOND + t.tv_nsec;
}

static void sleep_until(int64_t deadline)
{
    int64_t left = deadline - now_ns();
    if (left <= 0) {
        return;
    }
    struct timespec pause = {
        .tv_sec = (time_t)(left / NANOSECONDS_PER_SECOND),
        .tv_nsec = (long)(left % NANOSECONDS_PER_SECOND),
    };
    /* A signal (a window resize) can end the sleep early. That only
     * makes one frame come a little sooner, so it is not retried. */
    nanosleep(&pause, NULL);
}

int main(void)
{
    if (!term_open()) {
        fprintf(stderr, "agon: needs a terminal (do not redirect input or "
                        "output)\n");
        return 1;
    }

    int cols, rows;
    term_size(&cols, &rows);

    /* The grid is sized once, from the window at start-up (never smaller
     * than the minimum window), and keeps that size: resizing changes how
     * much of it is visible, not the grid. Two cells per character row,
     * and the last row is the status line. */
    int w = cols < MIN_COLS ? MIN_COLS : cols;
    int h = 2 * ((rows < MIN_ROWS ? MIN_ROWS : rows) - 1);
    size_t cell_count = (size_t)w * (size_t)h;

    /* calloc zeroes the memory, so every cell starts dead. malloc does
     * not: next starts as whatever bytes were there, which is fine because
     * step writes every cell of it before anyone reads it. */
    uint8_t *current = calloc(cell_count, sizeof(uint8_t));
    uint8_t *next = malloc(cell_count * sizeof(uint8_t));
    if (current == NULL || next == NULL) {
        term_close(); /* so the message lands on the normal screen */
        fprintf(stderr, "agon: out of memory\n");
        free(current); /* free(NULL) does nothing, so this is safe */
        free(next);
        return 1;
    }
    seed(current, w, h);

    long generation = 0;
    int speed = DEFAULT_SPEED;
    int paused = 0;
    int running = 1;
    int need_clear = 1;
    /* Generations owed, in thirtieths: each frame adds the speed, and
     * every 30 owed pays for one generation. Keeps whole numbers only. */
    int generations_due = 0;
    int64_t next_frame = now_ns();

    while (running) {
        if (term_resized()) {
            term_size(&cols, &rows);
            need_clear = 1;
        }

        int key;
        while ((key = term_key()) != -1) {
            if (key == 'q') {
                running = 0;
            } else if (key == ' ') {
                paused = !paused;
            } else if ((key == '+' || key == '=') && speed < SPEED_COUNT - 1) {
                speed++;
            } else if (key == '-' && speed > 0) {
                speed--;
            }
        }

        if (!paused) {
            generations_due += SPEEDS[speed];
            while (generations_due >= FRAMES_PER_SECOND) {
                step(current, next, w, h);
                /* Swap the pointers, not the cells (see lesson 1). */
                uint8_t *old = current;
                current = next;
                next = old;
                generation++;
                generations_due -= FRAMES_PER_SECOND;
            }
        }

        term_begin();
        if (need_clear) {
            term_clear();
            need_clear = 0;
        }
        if (cols < MIN_COLS || rows < MIN_ROWS) {
            draw_too_small(cols, rows);
        } else {
            draw_grid(current, w, h, cols, rows - 1);
            draw_status(rows - 1, cols, generation, speed, paused);
        }
        term_end();

        next_frame += FRAME_NANOSECONDS;
        int64_t now = now_ns();
        if (now > next_frame + FRAME_NANOSECONDS) {
            next_frame = now; /* fell far behind: do not try to catch up */
        }
        sleep_until(next_frame);
    }

    term_close();
    free(current);
    free(next);
    return 0;
}
