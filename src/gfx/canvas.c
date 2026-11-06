#include "canvas.h"

#include <stddef.h> /* size_t */
#include <stdio.h>  /* fopen, fprintf, fputc, fclose */
#include <stdlib.h> /* malloc, free */
#include <string.h> /* memcpy */

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

/*
 * A mask at scale 1 that lies wholly on the canvas: no clipping is possible, so
 * it writes pixels directly. This is the common case by far (most of a frame is
 * symbols), and it is worth having: going through canvas_rect once per pixel
 * cost a third of the program's time.
 */
static void mask_unclipped(Canvas *c, const uint16_t *rows, int size, int x,
                           int y, bool flip, uint32_t colour)
{
    uint32_t *line = c->px + y * c->w + x; /* a pointer to the mask's corner */
    for (int r = 0; r < size; r++) {
        unsigned bits = rows[r];
        if (bits != 0) {
            for (int i = 0; i < size; i++) {
                if ((bits >> (size - 1 - i)) & 1u) {
                    line[flip ? size - 1 - i : i] = colour;
                }
            }
        }
        line += c->w; /* adding to a uint32_t * moves by whole pixels */
    }
}

void canvas_mask(Canvas *c, const uint16_t *rows, int size, int x, int y,
                 int scale, bool flip, uint32_t colour)
{
    int extent = size * scale;
    if (scale == 1 && x >= 0 && y >= 0 && x + extent <= c->w &&
        y + extent <= c->h) {
        mask_unclipped(c, rows, size, x, y, flip, colour);
        return;
    }
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

void canvas_rect_clip(Canvas *c, int x, int y, int w, int h, uint32_t colour,
                      CanvasClip clip)
{
    /* The overlap of the two rectangles; canvas_rect then clips to the canvas. */
    int left = x > clip.x ? x : clip.x;
    int top = y > clip.y ? y : clip.y;
    int right = x + w < clip.x + clip.w ? x + w : clip.x + clip.w;
    int bottom = y + h < clip.y + clip.h ? y + h : clip.y + clip.h;
    if (left < right && top < bottom) {
        canvas_rect(c, left, top, right - left, bottom - top, colour);
    }
}

void canvas_mask_wide(Canvas *c, const uint64_t *rows, int size, int x, int y,
                      int scale, bool flip, uint32_t colour, CanvasClip clip)
{
    size = size > 64 ? 64 : size;
    for (int r = 0; r < size; r++) {
        uint64_t bits = rows[r];
        int i = 0;
        while (bits != 0 && i < size) {
            /* Column i is bit (size - 1 - i): skip clear pixels, then count
             * the run of set ones. */
            if (((bits >> (size - 1 - i)) & 1u) == 0) {
                i++;
                continue;
            }
            int start = i;
            while (i < size && ((bits >> (size - 1 - i)) & 1u)) {
                i++;
            }
            /* Mirrored, the run [start, i) lands at [size - i, size - start). */
            int column = flip ? size - i : start;
            canvas_rect_clip(c, x + column * scale, y + r * scale,
                             (i - start) * scale, scale, colour, clip);
        }
    }
}

void canvas_copy(Canvas *c, const Canvas *from, int x, int y)
{
    /* The part of from that lands on c, worked out once for all rows. */
    int left = x < 0 ? -x : 0;
    int top = y < 0 ? -y : 0;
    int right = x + from->w > c->w ? c->w - x : from->w;
    int bottom = y + from->h > c->h ? c->h - y : from->h;
    if (left >= right) {
        return; /* also means nothing is read through a NULL px below */
    }
    for (int r = top; r < bottom; r++) {
        /* memcpy counts bytes, not pixels. */
        memcpy(c->px + (y + r) * c->w + x + left, from->px + r * from->w + left,
               (size_t)(right - left) * sizeof(c->px[0]));
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
