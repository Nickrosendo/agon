#include <stdint.h> /* uint8_t: an unsigned integer exactly 8 bits wide */
#include <stdio.h>
#include <stdlib.h> /* malloc, calloc, free */
#include <time.h>   /* nanosleep, struct timespec (POSIX) */

#define WIDTH 40
#define HEIGHT 20
#define GENERATIONS 200
#define FRAME_NANOSECONDS 100000000L /* 100 ms */

/*
 * The grid is one flat array of WIDTH * HEIGHT cells, row after row.
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

static void print_grid(const uint8_t *grid, int w, int h, int generation)
{
    /* \033 is the escape character. "[H" moves the cursor to the top left
     * corner, "[2J" clears the screen. */
    printf("\033[H\033[2J");
    printf("generation %d\n", generation);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            putchar(grid[idx(x, y, w, h)] ? '#' : '.');
        }
        putchar('\n');
    }
    /* stdout keeps output in a buffer and writes it out in pieces;
     * fflush sends the whole frame now, before we sleep. */
    fflush(stdout);
}

static void seed(uint8_t *grid, int w, int h)
{
    /* A glider in the top left corner, travelling down and right:
     *   .#.
     *   ..#
     *   ###
     */
    grid[idx(1, 0, w, h)] = 1;
    grid[idx(2, 1, w, h)] = 1;
    grid[idx(0, 2, w, h)] = 1;
    grid[idx(1, 2, w, h)] = 1;
    grid[idx(2, 2, w, h)] = 1;

    /* A blinker: three in a row, flipping between flat and upright. It sits
     * off the glider's diagonal, so the two never meet as the glider wraps
     * around. */
    grid[idx(20, 10, w, h)] = 1;
    grid[idx(21, 10, w, h)] = 1;
    grid[idx(22, 10, w, h)] = 1;
}

int main(void)
{
    int w = WIDTH;
    int h = HEIGHT;
    /* malloc and calloc take a size_t (an unsigned type). The casts make
     * the int-to-unsigned conversion visible, and -Wconversion insists. */
    size_t cell_count = (size_t)w * (size_t)h;

    /* calloc zeroes the memory, so every cell starts dead. malloc does
     * not: next starts as whatever bytes were there, which is fine because
     * step writes every cell of it before anyone reads it. */
    uint8_t *current = calloc(cell_count, sizeof(uint8_t));
    uint8_t *next = malloc(cell_count * sizeof(uint8_t));
    if (current == NULL || next == NULL) {
        fprintf(stderr, "agon: out of memory\n");
        free(current); /* free(NULL) does nothing, so this is safe */
        free(next);
        return 1;
    }

    seed(current, w, h);

    struct timespec frame = {.tv_sec = 0, .tv_nsec = FRAME_NANOSECONDS};
    for (int generation = 0; generation < GENERATIONS; generation++) {
        print_grid(current, w, h, generation);
        step(current, next, w, h);

        /* Swap the two pointers, not the cells: next becomes the grid we
         * show, and the old grid becomes scratch space for the next step.
         * Only two addresses move; no cell is copied. */
        uint8_t *old = current;
        current = next;
        next = old;

        nanosleep(&frame, NULL);
    }

    free(current);
    free(next);
    return 0;
}
