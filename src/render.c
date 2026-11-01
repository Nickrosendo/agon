#include "render.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* memcpy, memset, strlen */

#include "genome.h"
#include "term.h"

/* The upper half block, U+2580, as its three UTF-8 bytes. The top half of
 * the character cell takes the foreground colour, the bottom half the
 * background colour, so one character shows two square cells. */
#define UPPER_HALF_BLOCK "\xE2\x96\x80"

/* How many soil colours, from dry to rich. */
#define SOIL_SHADES 6

/* How many colour steps a withering plant goes through on its way from
 * green to straw. */
#define WITHER_SHADES 4

/* The largest screen area whose last drawing is remembered. Cells beyond
 * it are simply redrawn every frame. */
#define MEMORY_COLS 512
#define MEMORY_ROWS 256

typedef struct {
    uint8_t r, g, b;
} Colour;

static const Colour WATER = {34, 66, 112};
static const Colour ROCK = {112, 112, 118};
static const Colour SOIL_DRY = {132, 110, 78};
static const Colour SOIL_RICH = {70, 56, 38};
static const Colour PLANT = {58, 150, 62};
static const Colour WITHERED = {196, 168, 84}; /* dry straw */
static const Colour CARRION = {110, 30, 34};   /* dried blood */
/* Each creature is coloured by the hue in its genome, light enough to
 * stand out from the terrain, and pulled towards a deep red the more of
 * its food is meat. The legend uses the same colours. */
#define CREATURE_SATURATION 0.55f /* 0 white .. 1 full colour */
static const Colour TEXT = {150, 155, 165};
static const Colour TEXT_BACKGROUND = {14, 17, 23};

/*
 * The legend's colours, in RENDER_SWATCH_ order. Pointers, not copies:
 * an initialiser outside a function must be a constant expression, and
 * in C (unlike C++) the value of a const variable is not one, so
 * {WATER, ROCK, ...} would not compile here. The address of a static
 * variable is a constant.
 */
static const Colour *const SWATCHES[RENDER_SWATCH_COUNT] = {
    &WATER, &ROCK, &SOIL_DRY, &SOIL_RICH, &PLANT, &WITHERED, &CARRION,
};

/*
 * What render_world last drew in each character cell: the top and bottom
 * colours packed into one number, with bit 48 set to mark "drawn". A
 * cell whose colours have not changed since the last frame is skipped,
 * so a calm world sends only a few bytes per frame instead of a full
 * screen. A static array: 1 MiB set aside for the whole run, zeroed at
 * start-up (zero means "never drawn"), never allocated or freed.
 */
static uint64_t last_drawn[MEMORY_ROWS][MEMORY_COLS];

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

static Colour soil_colour(float fertility)
{
    /* Fertility in a few steps rather than a smooth blend: neighbouring
     * cells then often share a colour, and term skips repeating a colour,
     * which keeps the frame small. Converting the float to int drops the
     * fraction. */
    int shade = (int)(fertility * (float)SOIL_SHADES);
    if (shade > SOIL_SHADES - 1) {
        shade = SOIL_SHADES - 1; /* fertility exactly 1.0 */
    }
    return mix(SOIL_DRY, SOIL_RICH, (float)shade / (float)(SOIL_SHADES - 1));
}

/* How far a withering plant has gone, in WITHER_SHADES steps from 1 (just
 * started) to WITHER_SHADES (about to disappear). Integer division
 * rounds down, so the + ticks - 1 rounds up instead: the first tick of
 * withering already shows a change of colour. */
static int wither_shade(int wither, int ticks)
{
    if (ticks < 1) {
        ticks = 1;
    }
    if (wither > ticks) {
        wither = ticks;
    }
    return (wither * WITHER_SHADES + ticks - 1) / ticks;
}

/*
 * A colour from a hue (0..1 round the colour wheel: 0 red, 1/3 green, 2/3
 * blue, back to red at 1), at full brightness and the given saturation.
 * The wheel has six sectors; in each, one channel is full, one is at its
 * lowest and the third climbs or falls between them.
 */
static Colour hue_colour(float hue, float saturation)
{
    float h6 = hue * 6.0f;
    int sector = (int)h6; /* drops the fraction: 0..5 for hue in 0..1 */
    float f = h6 - (float)sector;
    float lo = 1.0f - saturation;
    float rise = lo + saturation * f;
    float fall = 1.0f - saturation * f;
    float r, g, b;
    switch (sector) {
    case 0: r = 1.0f; g = rise; b = lo; break;
    case 1: r = fall; g = 1.0f; b = lo; break;
    case 2: r = lo; g = 1.0f; b = rise; break;
    case 3: r = lo; g = fall; b = 1.0f; break;
    case 4: r = rise; g = lo; b = 1.0f; break;
    default: r = 1.0f; g = lo; b = fall; break; /* 5, or hue exactly 1 */
    }
    /* Each channel is in 0..1, so * 255 + 0.5 stays inside the range a
     * uint8_t can hold and the conversion is safe. */
    Colour c = {(uint8_t)(r * 255.0f + 0.5f), (uint8_t)(g * 255.0f + 0.5f),
                (uint8_t)(b * 255.0f + 0.5f)};
    return c;
}

float render_meat_share(float level)
{
    float t = level - 2.0f;
    if (t < 0.0f) {
        return 0.0f;
    }
    return t > 1.0f ? 1.0f : t;
}

/* A family's hue moved t of the way round the wheel to red (hue 0),
 * whichever way round is shorter: a blue creature (hue 0.67) passes
 * through violet, a yellow one (0.17) through orange. */
static float hue_towards_red(float hue, float t)
{
    float step = hue > 0.5f ? 1.0f - hue : -hue; /* signed distance to 0 */
    float h = hue + step * t;
    return h >= 1.0f ? h - 1.0f : h; /* only 1.0 itself can come out */
}

static Colour creature_colour(float hue, float meat)
{
    /* Plant-eaters: pale family colours. Meat-eaters: deep red, with the
     * saturation climbing from 0.55 to 1 as the hue turns. */
    float saturation =
        CREATURE_SATURATION + (1.0f - CREATURE_SATURATION) * meat;
    return hue_colour(hue_towards_red(hue, meat), saturation);
}

static Colour cell_colour(const Sim *s, int i)
{
    const World *w = &s->world;
    const Settings *set = &s->set;
    /* A creature hides whatever is under it. occupant holds its slot in
     * the pool, so its genome is one lookup away. */
    if (w->occupant[i] >= 0) {
        const Creature *c = &s->pool.items[w->occupant[i]];
        return creature_colour(c->genome.g[GENE_HUE],
                               render_meat_share(c->level));
    }
    if (w->terrain[i] == TERRAIN_WATER) {
        return WATER;
    }
    if (w->terrain[i] == TERRAIN_ROCK) {
        return ROCK;
    }
    Colour soil = soil_colour(w->fertility[i]);
    if (w->carrion[i] > 0.0f) {
        /* Carrion covers whatever grows there: from a faint stain to
         * full colour as it approaches several bites' worth, in a few
         * steps so neighbouring cells often share a colour. */
        float amount = w->carrion[i] / (w->carrion[i] + set->bite_meat_energy);
        int shade = 1 + (int)(amount * 3.0f); /* 1..3, 4 only at amount 1 */
        return mix(soil, CARRION, (float)shade / 4.0f);
    }
    if (w->veg[i] == 0) {
        return soil;
    }
    /* Four shades of green over the soil: density 1 is 40 % of the way
     * from the soil colour to full green, density VEG_MAX is all green. */
    float t = (float)(w->veg[i] + 1) / (float)(VEG_MAX + 1);
    Colour plant = mix(soil, PLANT, t);
    if (w->veg_wither[i] == 0) {
        return plant;
    }
    /* Withering: from the plant's green towards straw, in a few steps. */
    int shade = wither_shade(w->veg_wither[i], set->veg_wither_ticks);
    return mix(plant, WITHERED, (float)shade / (float)WITHER_SHADES);
}

static uint64_t pack(Colour top, Colour bottom)
{
    /* The casts widen each byte to 64 bits before shifting; shifting a
     * value past the width of its type is undefined behaviour. */
    uint64_t t = ((uint64_t)top.r << 16) | ((uint64_t)top.g << 8) | top.b;
    uint64_t b =
        ((uint64_t)bottom.r << 16) | ((uint64_t)bottom.g << 8) | bottom.b;
    return (1ULL << 48) | (t << 24) | b;
}

void render_invalidate(void)
{
    /* memset with sizeof works here because last_drawn is a real array
     * in this file, not a pointer: sizeof gives all 1 MiB. */
    memset(last_drawn, 0, sizeof(last_drawn));
}

void render_world(const Sim *s, int col0, int row0, int cols, int rows)
{
    const World *w = &s->world;
    int view_cols = cols < w->w ? cols : w->w;
    int view_rows = rows < w->h / 2 ? rows : w->h / 2;
    for (int row = 0; row < view_rows; row++) {
        /* The cursor is only moved when a cell is skipped: after a
         * character is drawn, the terminal moves it one column right by
         * itself. */
        int cursor_in_place = 0;
        for (int x = 0; x < view_cols; x++) {
            Colour top = cell_colour(s, world_idx(w, x, 2 * row));
            Colour bottom = cell_colour(s, world_idx(w, x, 2 * row + 1));
            uint64_t packed = pack(top, bottom);
            int remembered = row < MEMORY_ROWS && x < MEMORY_COLS;
            if (remembered && last_drawn[row][x] == packed) {
                cursor_in_place = 0;
                continue;
            }
            if (remembered) {
                last_drawn[row][x] = packed;
            }
            if (!cursor_in_place) {
                term_move(col0 + x, row0 + row);
                cursor_in_place = 1;
            }
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

void render_swatch(int col, int row, int swatch)
{
    if (swatch < 0 || swatch >= RENDER_SWATCH_COUNT) {
        return; /* indexing SWATCHES with it would read out of bounds */
    }
    const Colour *c = SWATCHES[swatch];
    term_move(col, row);
    term_bg(c->r, c->g, c->b);
    term_put(" ");
}

void render_creature_swatch(int col, int row, float hue, float meat)
{
    /* The same function the map uses, so the legend cannot drift from
     * what is drawn. */
    Colour c = creature_colour(hue, meat);
    term_move(col, row);
    term_bg(c.r, c.g, c.b);
    term_put(" ");
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
