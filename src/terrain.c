#include "terrain.h"

#include <math.h> /* lroundf */

/* Added to the seed for each octave, so each layer gets its own values.
 * A typed constant rather than a #define: a ULL literal is unsigned long
 * long, while uint64_t here is unsigned long, and mixing the two in one
 * expression trips -Wsign-conversion although both are 64 bits. */
static const uint64_t OCTAVE_SEED_STEP = 0x9E3779B97F4A7C15ULL;

/*
 * The random value at lattice point (ix, iy), in 0..1. Instead of storing
 * a grid of random numbers (which would need memory allocated for each
 * new world), the value is computed from the seed and the coordinates by
 * a hash: the same inputs always give the same value, and neighbouring
 * points give unrelated ones.
 */
static float lattice_value(uint64_t seed, int ix, int iy)
{
    /* Multiplying by large odd constants spreads the coordinates over all
     * 64 bits before they are mixed. The casts go through uint32_t so a
     * coordinate becomes an unsigned number without sign extension. */
    uint64_t state = seed ^ ((uint64_t)(uint32_t)ix * 0xD1B54A32D192ED03ULL) ^
                     ((uint64_t)(uint32_t)iy * 0xABC98388FB8FAC03ULL);
    uint32_t top = (uint32_t)(rng_splitmix64(&state) >> 40);
    return (float)top / 16777216.0f; /* 24 bits, as in rng_unit */
}

/* An S-shaped curve from 0 to 1 with a flat start and end, so the slope
 * does not jump at lattice points (no visible grid lines). */
static float smooth(float t)
{
    return t * t * (3.0f - 2.0f * t);
}

/* How many lattice points fit across size cells at the given spacing:
 * at least 1, at most one per cell. */
static int lattice_count(int size, float spacing)
{
    int n = (int)lroundf((float)size / spacing);
    if (n < 1) {
        return 1;
    }
    return n > size ? size : n;
}

/*
 * One layer: nx by ny lattice points spread evenly over the w by h grid,
 * with smooth interpolation between the four around (x, y). The point
 * after the last one is point 0 again (% nx), which is what makes the
 * noise tile: the right edge blends into the left edge.
 */
static float layer_value(uint64_t seed, int x, int y, int w, int h, int nx,
                         int ny)
{
    /* Cell position in lattice units. ints are converted to float
     * explicitly; -Wconversion warns because a large int does not fit
     * exactly in a float's 24 bits. */
    float fx = (float)x * (float)nx / (float)w;
    float fy = (float)y * (float)ny / (float)h;
    /* Converting a float to int drops the fraction (rounds toward zero);
     * fx is never negative here, so that is the floor. */
    int ix = (int)fx;
    int iy = (int)fy;
    float tx = smooth(fx - (float)ix);
    float ty = smooth(fy - (float)iy);

    int x0 = ix % nx, x1 = (ix + 1) % nx;
    int y0 = iy % ny, y1 = (iy + 1) % ny;
    float top_left = lattice_value(seed, x0, y0);
    float top_right = lattice_value(seed, x1, y0);
    float bottom_left = lattice_value(seed, x0, y1);
    float bottom_right = lattice_value(seed, x1, y1);

    float top = top_left + (top_right - top_left) * tx;
    float bottom = bottom_left + (bottom_right - bottom_left) * tx;
    return top + (bottom - top) * ty;
}

float terrain_noise(uint64_t seed, int x, int y, int w, int h, float scale,
                    int octaves)
{
    int nx = lattice_count(w, scale);
    int ny = lattice_count(h, scale);
    float total = 0.0f;
    float strength = 1.0f;
    float strength_sum = 0.0f;
    for (int octave = 0; octave < octaves; octave++) {
        /* Unsigned: the seed may wrap around past 2^64, which is defined. */
        uint64_t octave_seed = seed + (uint64_t)octave * OCTAVE_SEED_STEP;
        total += strength * layer_value(octave_seed, x, y, w, h, nx, ny);
        strength_sum += strength;
        strength *= 0.5f;
        nx = nx * 2 > w ? w : nx * 2;
        ny = ny * 2 > h ? h : ny * 2;
    }
    /* Dividing by the total strength brings the sum back to 0..1. */
    return total / strength_sum;
}

void terrain_generate(World *w, Rng *r, const Settings *s)
{
    uint64_t height_seed = rng_next(r);
    uint64_t fertility_seed = rng_next(r);

    /* Summed layers cluster around the middle, and how much depends on
     * the octaves and the world size. Spreading each world's heights over
     * the full 0..1 makes water_level and rock_level mean the same thing
     * everywhere. First pass: find the lowest and highest point. */
    float lowest = 1.0f, highest = 0.0f;
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            float v = terrain_noise(height_seed, x, y, w->w, w->h,
                                    s->noise_scale, s->noise_octaves);
            lowest = v < lowest ? v : lowest;
            highest = v > highest ? v : highest;
        }
    }
    float range = highest > lowest ? highest - lowest : 1.0f;

    /* Second pass: compute each height again (cheaper to recompute than
     * to keep a whole extra layer), spread it, and classify it. */
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            int i = world_idx(w, x, y);
            float v = terrain_noise(height_seed, x, y, w->w, w->h,
                                    s->noise_scale, s->noise_octaves);
            float height = (v - lowest) / range;
            if (height < s->water_level) {
                w->terrain[i] = height >= s->water_level - s->shallow_band
                                    ? TERRAIN_SHALLOW
                                    : TERRAIN_WATER;
                w->fertility[i] = 0.0f;
            } else if (height >= s->rock_level) {
                w->terrain[i] = TERRAIN_ROCK;
                w->fertility[i] = 0.0f;
            } else {
                w->terrain[i] = TERRAIN_SOIL;
                w->fertility[i] =
                    terrain_noise(fertility_seed, x, y, w->w, w->h,
                                  s->fertility_scale, s->noise_octaves);
            }
        }
    }
}
