#include "render.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* memcpy, memset, strlen */

#include "term.h"

/* The upper half block, U+2580, as its three UTF-8 bytes. The top half of
 * the character cell takes the foreground colour, the bottom half the
 * background colour, so one character shows two square cells. */
#define UPPER_HALF_BLOCK "\xE2\x96\x80"

/* How many soil colours, from dry to rich. */
#define SOIL_SHADES 6

typedef struct {
    uint8_t r, g, b;
} Colour;

static const Colour WATER = {34, 66, 112};
static const Colour ROCK = {112, 112, 118};
static const Colour SOIL_DRY = {132, 110, 78};
static const Colour SOIL_RICH = {70, 56, 38};
static const Colour PLANT = {104, 184, 88};
static const Colour TEXT = {150, 155, 165};
static const Colour TEXT_BACKGROUND = {14, 17, 23};

/* A struct is compared field by field; C has no == for structs. */
static int same_colour(Colour a, Colour b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* One colour channel, t of the way from a to b (t in 0..1). */
static uint8_t mix_channel(uint8_t a, uint8_t b, float t)
{
    float v = (float)a + ((float)b - (float)a) * t;
    /* + 0.5f then dropping the fraction rounds to the nearest whole
     * number. v stays inside 0..255, so the conversion is safe; outside
     * that range, converting a float to uint8_t is undefined behaviour. */
    return (uint8_t)(v + 0.5f);
}

static Colour mix(Colour a, Colour b, float t)
{
    Colour c = {mix_channel(a.r, b.r, t), mix_channel(a.g, b.g, t),
                mix_channel(a.b, b.b, t)};
    return c;
}

static Colour cell_colour(const World *w, int i)
{
    if (w->veg[i] > 0) {
        return PLANT;
    }
    if (w->terrain[i] == TERRAIN_WATER) {
        return WATER;
    }
    if (w->terrain[i] == TERRAIN_ROCK) {
        return ROCK;
    }
    /* Fertility in a few steps rather than a smooth blend: neighbouring
     * cells then often share a colour, and term skips repeating a colour,
     * which keeps the frame small. Converting the float to int drops the
     * fraction. */
    int shade = (int)(w->fertility[i] * (float)SOIL_SHADES);
    if (shade > SOIL_SHADES - 1) {
        shade = SOIL_SHADES - 1; /* fertility exactly 1.0 */
    }
    return mix(SOIL_DRY, SOIL_RICH, (float)shade / (float)(SOIL_SHADES - 1));
}

void render_world(const World *w, int col0, int row0, int cols, int rows)
{
    int view_cols = cols < w->w ? cols : w->w;
    int view_rows = rows < w->h / 2 ? rows : w->h / 2;
    for (int row = 0; row < view_rows; row++) {
        term_move(col0, row0 + row);
        for (int x = 0; x < view_cols; x++) {
            Colour top = cell_colour(w, world_idx(w, x, 2 * row));
            Colour bottom = cell_colour(w, world_idx(w, x, 2 * row + 1));
            if (same_colour(top, bottom)) {
                /* A space in the background colour looks the same and is
                 * 1 byte instead of 3. */
                term_bg(top.r, top.g, top.b);
                term_put(" ");
            } else {
                term_fg(top.r, top.g, top.b);
                term_bg(bottom.r, bottom.g, bottom.b);
                term_put(UPPER_HALF_BLOCK);
            }
        }
    }
}

void render_text(int col, int row, const char *text, int max_cols)
{
    char line[512];
    size_t width = max_cols > 0 ? (size_t)max_cols : 0;
    if (width > sizeof(line) - 1) {
        width = sizeof(line) - 1;
    }
    /* Only for ASCII: there one byte is one column on screen. A UTF-8
     * character of several bytes would be miscounted, or cut in half. */
    size_t len = strlen(text);
    if (len > width) {
        len = width;
    }
    memcpy(line, text, len);
    memset(line + len, ' ', width - len);
    line[width] = '\0';

    term_move(col, row);
    term_fg(TEXT.r, TEXT.g, TEXT.b);
    term_bg(TEXT_BACKGROUND.r, TEXT_BACKGROUND.g, TEXT_BACKGROUND.b);
    term_put(line);
}

void render_too_small(int cols, int rows, int min_cols, int min_rows)
{
    render_text(0, 0, "window too small", cols);
    if (rows > 1) {
        char need[48];
        snprintf(need, sizeof(need), "need %d x %d", min_cols, min_rows);
        render_text(0, 1, need, cols);
    }
}
