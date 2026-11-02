#include "tiles.h"

#include <math.h>   /* fabsf, sinf */
#include <string.h> /* memset */

#define TWO_PI 6.2831853f

/*
 * Mixes three small numbers into one 32-bit number, so nearby inputs give
 * unrelated outputs. Unsigned arithmetic on purpose: when an unsigned
 * multiplication overflows it wraps round, which C defines. The same on a
 * signed int would be undefined behaviour.
 */
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 374761393u + b * 668265263u + c * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

int tiles_variant(int x, int y)
{
    /* The top bits are the best mixed: >> 30 leaves 0..3. */
    return (int)(hash3((uint32_t)x, (uint32_t)y, 1u) >> 30);
}

static void set_pixel(uint16_t rows[16], int size, int i, int j)
{
    if (i >= 0 && i < size && j >= 0 && j < size) {
        rows[j] = (uint16_t)(rows[j] | (1u << (size - 1 - i)));
    }
}

/* Two wave lines, drifting sideways from frame to frame. */
static void build_water(uint16_t rows[16], int variant, int size, int frame)
{
    float phase = ((float)frame + (float)variant) * TWO_PI /
                  (float)TILE_WATER_FRAMES;
    float thickness = size > 8 ? 0.05f : 0.08f;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            float u = ((float)i + 0.5f) / (float)size;
            float v = ((float)j + 0.5f) / (float)size;
            float wave = sinf(u * TWO_PI + phase) * 0.07f;
            if (fabsf(v - 0.3f - wave) < thickness ||
                fabsf(v - 0.72f - wave) < thickness) {
                set_pixel(rows, size, i, j);
            }
        }
    }
}

/* Speckle in blocks of two pixels at the larger size. */
static void build_rock(uint16_t rows[16], int variant, int size)
{
    int shift = size > 8 ? 1 : 0;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            uint32_t h = hash3((uint32_t)(i >> shift), (uint32_t)(j >> shift),
                               4u + (uint32_t)variant);
            if ((h >> 24) < 140u) { /* a little over half of 0..255 */
                set_pixel(rows, size, i, j);
            }
        }
    }
}

/* A few separate dots, one more for each step of fertility. */
static void build_soil(uint16_t rows[16], int variant, int detail, int size)
{
    int dots = 1 + detail;
    int inner = size - 2;
    int placed = 0;
    /* A taken spot is skipped, so more dots always means more pixels. */
    for (uint32_t k = 0; placed < dots && k < 64u; k++) {
        int i = 1 + (int)(hash3((uint32_t)variant, k, 5u) % (uint32_t)inner);
        int j = 1 + (int)(hash3((uint32_t)variant, k, 6u) % (uint32_t)inner);
        unsigned bit = 1u << (size - 1 - i);
        if ((rows[j] & bit) == 0) {
            set_pixel(rows, size, i, j);
            placed++;
        }
    }
}

/* A stem and a round crown, both growing with density; density 0 is bare.
 * The variant leans the crown a pixel to one side, so a meadow is not a
 * grid of identical trees. */
static void build_plant(uint16_t rows[16], int variant, int detail, int size)
{
    if (detail <= 0) {
        return;
    }
    float d = (float)detail / 4.0f;
    float lean = (float)(variant % 3 - 1) / (float)size;
    float crown_x = 0.5f + lean;
    float crown_y = 0.72f - 0.34f * d;
    float crown_r = detail == 1 ? 0.0f : 0.10f + 0.20f * d;
    float half_stem = 0.5f / (float)size + 0.001f;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            float u = ((float)i + 0.5f) / (float)size;
            float v = ((float)j + 0.5f) / (float)size;
            float dx = u - crown_x, dy = v - crown_y;
            int in_crown = dx * dx + dy * dy < crown_r * crown_r;
            int in_stem = fabsf(u - 0.5f) < half_stem && v > crown_y && v < 0.92f;
            int in_base = fabsf(v - 0.88f) < half_stem &&
                          fabsf(u - 0.5f) < 0.06f + 0.14f * d;
            if (in_crown || in_stem || in_base) {
                set_pixel(rows, size, i, j);
            }
        }
    }
}

/* A bone: a line with a knob at each end, tilted by the variant. */
static void build_carrion(uint16_t rows[16], int variant, int size)
{
    float tilt = (float)(variant - 2) * 0.12f + 0.06f;
    float half = size > 8 ? 0.30f : 0.28f;
    float knob = size > 8 ? 0.10f : 0.13f;
    float x0 = 0.5f - half, y0 = 0.5f - tilt;
    float x1 = 0.5f + half, y1 = 0.5f + tilt;
    float line = 0.5f / (float)size + 0.01f;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            float u = ((float)i + 0.5f) / (float)size;
            float v = ((float)j + 0.5f) / (float)size;
            float along_v = y0 + (y1 - y0) * (u - x0) / (x1 - x0);
            int in_line = u > x0 && u < x1 && fabsf(v - along_v) < line;
            float a = u - x0, b = v - y0, c = u - x1, e = v - y1;
            int in_knob = a * a + b * b < knob * knob ||
                          c * c + e * e < knob * knob;
            if (in_line || in_knob) {
                set_pixel(rows, size, i, j);
            }
        }
    }
}

void tiles_build(uint16_t rows[16], int kind, int variant, int detail, int size,
                 int frame)
{
    /* sizeof(rows) would be wrong here: an array parameter is really a
     * pointer, and sizeof would give the pointer's size. Hence 16 * .... */
    memset(rows, 0, 16 * sizeof(rows[0]));
    switch (kind) {
    case TILE_WATER: build_water(rows, variant, size, frame); break;
    case TILE_ROCK: build_rock(rows, variant, size); break;
    case TILE_SOIL: build_soil(rows, variant, detail, size); break;
    case TILE_PLANT: build_plant(rows, variant, detail, size); break;
    case TILE_CARRION: build_carrion(rows, variant, size); break;
    default: break;
    }
}
