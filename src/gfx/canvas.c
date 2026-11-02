#include "canvas.h"

#include <stddef.h> /* size_t */
#include <stdio.h>  /* fopen, fprintf, fputc, fclose */
#include <stdlib.h> /* malloc, free */

uint32_t canvas_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    /* The casts come first: a uint8_t in an expression becomes a signed
     * int, and shifting into an int's top bit is undefined behaviour. */
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

/* How many pixels w by h is, or -1 when that does not fit an int. In size_t,
 * 64 bits wide here, the product of two ints cannot wrap around, so the check
 * sees the true answer; in int, the multiplication itself could overflow. */
static int pixel_count(int w, int h)
{
    size_t n = (size_t)w * (size_t)h;
    return n > (size_t)2147483647 ? -1 : (int)n;
}

bool canvas_init(Canvas *c, int w, int h)
{
    *c = (Canvas){0}; /* px NULL, sizes 0: an empty canvas */
    return canvas_resize(c, w, h);
}

bool canvas_resize(Canvas *c, int w, int h)
{
    w = w < 0 ? 0 : w;
    h = h < 0 ? 0 : h;
    int needed = pixel_count(w, h);
    if (needed < 0) {
        return false;
    }
    if (needed > c->capacity) {
        /* Not realloc: the old pixels need not survive, and asking for the
         * new block first means a failure leaves the old one in place. */
        uint32_t *block = malloc((size_t)needed * sizeof(*block));
        if (block == NULL) {
            return false;
        }
        free(c->px);
        c->px = block;
        c->capacity = needed;
    }
    c->w = w;
    c->h = h;
    canvas_fill(c, canvas_rgb(0, 0, 0));
    return true;
}

void canvas_free(Canvas *c)
{
    free(c->px); /* free(NULL) is allowed and does nothing */
    *c = (Canvas){0};
}

void canvas_fill(Canvas *c, uint32_t colour)
{
    canvas_rect(c, 0, 0, c->w, c->h, colour);
}

void canvas_rect(Canvas *c, int x, int y, int w, int h, uint32_t colour)
{
    /* Clip to the block. C checks nothing: writing to px[-1] or past the
     * end would overwrite whatever lives there, with no error. */
    int x0 = x < 0 ? 0 : x;
    int y0 = y < 0 ? 0 : y;
    int x1 = x + w > c->w ? c->w : x + w;
    int y1 = y + h > c->h ? c->h : y + h;
    for (int py = y0; py < y1; py++) {
        uint32_t *row = c->px + py * c->w; /* pointer sum: counts in pixels */
        for (int px = x0; px < x1; px++) {
            row[px] = colour;
        }
    }
}

void canvas_mask(Canvas *c, const uint16_t *rows, int size, int x, int y,
                 int scale, bool flip, uint32_t colour)
{
    for (int r = 0; r < size; r++) {
        /* The cast keeps the shift below unsigned: a uint16_t would become
         * a signed int first. */
        unsigned bits = rows[r];
        if (bits == 0) {
            continue; /* most rows of most symbols are short or empty */
        }
        for (int i = 0; i < size; i++) {
            if ((bits >> (size - 1 - i)) & 1u) {
                int column = flip ? size - 1 - i : i;
                canvas_rect(c, x + column * scale, y + r * scale, scale, scale,
                            colour);
            }
        }
    }
}

bool canvas_write_ppm(const Canvas *c, const char *path)
{
    /* "wb": binary, or Windows would turn each byte 10 into two bytes. */
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    fprintf(f, "P6\n%d %d\n255\n", c->w, c->h);
    for (int i = 0; i < c->w * c->h; i++) {
        uint32_t p = c->px[i];
        fputc((int)((p >> 16) & 0xFFu), f);
        fputc((int)((p >> 8) & 0xFFu), f);
        fputc((int)(p & 0xFFu), f);
    }
    /* Writes are buffered, so a full disk may only show up here. */
    bool wrote = !ferror(f);
    return fclose(f) == 0 && wrote;
}
