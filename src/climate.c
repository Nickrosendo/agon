#include "climate.h"

#include <math.h> /* cosf, sinf */

#include "terrain.h" /* terrain_noise */
#include "years.h"   /* years_season_of, years_season_starts */

/* Odd and large, so frame numbers land far apart among the 64-bit seeds, and
 * not the constant terrain_noise steps its octaves by: frames must not reuse
 * each other's layers. Typed for the reason given in terrain.c. */
static const uint64_t RAIN_FRAME_STEP = 0xD6E8FEB86659FD93ULL;

#define HALF_PI 1.5707963f
/* The rain pattern is smooth blobs: two layers are plenty, and it is worked out
 * for every cell on each update. */
#define RAIN_OCTAVES 2
#define TEMPERATURE_OCTAVES 2
/* A world's first water, from how far its noise value lies between these: no
 * water below the first, full above the second. */
#define START_DRY 0.4f
#define START_WET 0.65f

/*
 * One moment of the rain's slow change: two noise fields and how much of each.
 * Worked out once per update rather than per cell, since cosf and sinf are not
 * free and every cell at one tick would get the same answers.
 */
typedef struct {
    uint64_t seed_from, seed_to;
    float share_from, share_to;
    float threshold; /* noise above this rains, at this time of the cycle */
} RainMoment;

/* The threshold moves along a cosine: lowest, so wettest, at the start of each
 * cycle, and highest half way through. A threshold of 1 or more is left alone,
 * since it is the settings' way of saying no rain ever. Summer raises it by
 * summer_rain_rise on top, whatever the cycle is doing: the two are separate,
 * so a summer in a dry time is drier than one in a wet time. The result may
 * pass 1; rain_from_noise reads that as no rain, without dividing by zero. */
static float threshold_at(const Settings *s, uint64_t tick)
{
    if (s->rain_threshold >= 1.0f) {
        return s->rain_threshold;
    }
    float phase = (float)(tick % (uint64_t)s->rain_cycle) / (float)s->rain_cycle;
    float threshold =
        s->rain_threshold - s->rain_swing * cosf(phase * 4.0f * HALF_PI);
    if (years_season_of(tick, s->ticks_per_year) == SEASON_SUMMER) {
        threshold += s->summer_rain_rise;
    }
    return threshold;
}

static RainMoment rain_moment(const World *w, const Settings *s, uint64_t tick)
{
    uint64_t period = (uint64_t)s->rain_period;
    uint64_t frame = tick / period;
    float phase = (float)(tick % period) / (float)period;
    /*
     * Why a quarter circle and not a straight-line blend: adding two unrelated
     * noise fields as (1-t)a + tb flattens their peaks half way, by a factor of
     * about 0.7, so rain would weaken and return with every fade. With cos and
     * sin the squares of the shares always add up to 1, and the mix keeps the
     * same spread at every moment.
     */
    RainMoment m = {
        .seed_from = w->rain_seed + frame * RAIN_FRAME_STEP,
        .seed_to = w->rain_seed + (frame + 1) * RAIN_FRAME_STEP,
        .share_from = cosf(phase * HALF_PI),
        .share_to = sinf(phase * HALF_PI),
        .threshold = threshold_at(s, tick),
    };
    return m;
}

/* The mixed noise at a cell: 0..1 in practice, around 0.5. */
static float rain_noise(const World *w, const Settings *s, const RainMoment *m,
                        int x, int y)
{
    float a = terrain_noise(m->seed_from, x, y, w->w, w->h, s->rain_scale,
                            RAIN_OCTAVES);
    float b = terrain_noise(m->seed_to, x, y, w->w, w->h, s->rain_scale,
                            RAIN_OCTAVES);
    return 0.5f + (a - 0.5f) * m->share_from + (b - 0.5f) * m->share_to;
}

static float clamp_unit(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* Only noise above the threshold is rain, scaled so its top reaches 1. */
static float rain_from_noise(float noise, float threshold)
{
    if (threshold >= 1.0f) {
        return 0.0f; /* the range below would divide by zero */
    }
    return clamp_unit((noise - threshold) / (1.0f - threshold));
}

float climate_rain_at(const World *w, const Settings *s, int x, int y,
                      uint64_t tick)
{
    RainMoment m = rain_moment(w, s, tick);
    return rain_from_noise(rain_noise(w, s, &m, x, y), m.threshold);
}

/* The field spread over the whole of 0..1 for this world, as terrain does with
 * heights: otherwise the region limits would mean different things in
 * different worlds. */
static void generate_temperature(World *w, uint64_t seed, const Settings *s)
{
    float lowest = 1.0f, highest = 0.0f;
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            float v = terrain_noise(seed, x, y, w->w, w->h, s->temperature_scale,
                                    TEMPERATURE_OCTAVES);
            w->temperature[world_idx(w, x, y)] = v;
            lowest = v < lowest ? v : lowest;
            highest = v > highest ? v : highest;
        }
    }
    float range = highest > lowest ? highest - lowest : 1.0f;
    for (int i = 0; i < w->w * w->h; i++) {
        w->temperature[i] = (w->temperature[i] - lowest) / range;
    }
}

void climate_generate(World *w, Rng *r, const Settings *s)
{
    uint64_t temperature_seed = rng_next(r);
    w->rain_seed = rng_next(r);
    generate_temperature(w, temperature_seed, s);

    /* The world starts as if it had been raining this way for a while, so that
     * the first ticks are not a wave of drying. */
    RainMoment m = rain_moment(w, s, 0);
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            int i = world_idx(w, x, y);
            float noise = rain_noise(w, s, &m, x, y);
            w->rain[i] = rain_from_noise(noise, m.threshold);
            w->water[i] = w->terrain[i] == TERRAIN_SOIL
                              ? clamp_unit((noise - START_DRY) /
                                           (START_WET - START_DRY))
                              : 0.0f;
        }
    }
}

/* The first tick of spring: every cell's snow goes into its soil at once,
 * topping the water up to full at most, and whatever does not fit is lost
 * (runs off). Non-soil cells never hold snow, so they need no test here. */
static void melt_snow(World *w)
{
    for (int i = 0; i < w->w * w->h; i++) {
        w->water[i] = clamp_unit(w->water[i] + w->snow[i]);
        w->snow[i] = 0.0f;
    }
}

void climate_step(World *w, const Settings *s, uint64_t tick)
{
    /* Before the climate_every test, so the melt happens on spring's first
     * tick even when the updates fall on other ticks. Tick 0 is a spring
     * start too, but a new world has no snow; skipping it keeps the first
     * tick of a world exactly as it was before snow existed. */
    if (tick > 0 &&
        years_season_starts(tick, s->ticks_per_year, SEASON_SPRING)) {
        melt_snow(w);
    }
    if (tick % (uint64_t)s->climate_every != 0) {
        return;
    }
    RainMoment m = rain_moment(w, s, tick);
    float ticks = (float)s->climate_every;
    bool winter = years_season_of(tick, s->ticks_per_year) == SEASON_WINTER;
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            int i = world_idx(w, x, y);
            float rain = rain_from_noise(rain_noise(w, s, &m, x, y), m.threshold);
            w->rain[i] = rain;
            if (w->terrain[i] != TERRAIN_SOIL) {
                continue;
            }
            /* Soil takes water more slowly the wetter it is, and loses it
             * faster: so it settles somewhere between, and never leaves 0..1
             * unless the settings make one update overshoot; hence the clamp.
             * In winter the rain lies on top as snow: it piles up at the full
             * rate, since nothing is soaking in, and the soil only dries. */
            float water = w->water[i];
            float lost = s->water_loss * water;
            if (winter) {
                float fallen = s->rain_gain * rain * ticks;
                w->snow[i] = clamp_unit(w->snow[i] + fallen);
                w->water[i] = clamp_unit(water - lost * ticks);
                continue;
            }
            float gained = s->rain_gain * rain * (1.0f - water);
            w->water[i] = clamp_unit(water + (gained - lost) * ticks);
        }
    }
}

int climate_region(const World *w, int cell, const Settings *s)
{
    if (w->terrain[cell] == TERRAIN_SHALLOW) {
        return REGION_SHALLOW;
    }
    if (w->terrain[cell] != TERRAIN_SOIL) {
        return REGION_NONE;
    }
    float temperature = w->temperature[cell];
    float water = w->water[cell];
    if (temperature < s->region_cold_below) {
        return REGION_COLD;
    }
    if (water > s->region_wet_above) {
        return REGION_WET;
    }
    if (temperature > s->region_hot_above && water < s->region_dry_below) {
        return REGION_HOT_DRY;
    }
    return REGION_TEMPERATE;
}

float climate_region_hue(int region)
{
    /* In the order of the enum. Blue-white snow, yellow sand, green grass, the
     * blue-green of a marsh, the blue of the shallows. */
    static const float HUES[REGION_COUNT] = {0.58f, 0.11f, 0.30f, 0.42f, 0.52f};
    return HUES[region];
}
