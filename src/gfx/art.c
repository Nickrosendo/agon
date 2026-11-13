#include "art.h"

#include <math.h>   /* ceilf, floorf, sqrtf */
#include <string.h> /* memset */

/* The most samples taken across one grid pixel, each way: enough for a
 * picture shrunk to an eighth. */
#define ART_SAMPLES_MAX 8

const ArtSet *art_set(int species)
{
    for (int i = 0; i < ART_SET_COUNT; i++) {
        if (ART_SETS[i].species == species) {
            return &ART_SETS[i];
        }
    }
    return NULL;
}

const ArtClip *art_clip(int species, int action, const BodyTraits *traits)
{
    const ArtSet *set = art_set(species);
    (void)traits;  /* every body uses its species' frames, parts or not */
    if (set == NULL || action < 0 || action >= ACTION_COUNT) {
        return NULL;
    }
    const ArtClip *clip = &set->clip[action];
    return clip->frames > 0 ? clip : NULL;
}

/* One part's layer clip for this action, or NULL: the size the gene asks
 * for, or the full one when that size was not painted. gene is the rounded
 * one, so 1 means full and anything from PART_FROM below it medium. */
static const ArtClip *layer_for(const ArtSet *set, int part, float gene, int action)
{
    if (gene < PART_FROM) {
        return NULL;
    }
    int size = gene >= 1.0f ? ART_SIZE_FULL : ART_SIZE_MEDIUM;
    const ArtClip *clips = set->layer[part][size];
    if (clips == NULL) {
        clips = set->layer[part][ART_SIZE_FULL];
    }
    if (clips == NULL || clips[action].frames <= 0) {
        return NULL;
    }
    /* clips points at ACTION_COUNT clips in a row; + action steps to this
     * action's, the same as &clips[action]. */
    return clips + action;
}

int art_layers(int species, int action, const BodyTraits *traits,
               const ArtClip *out[ART_PART_COUNT])
{
    for (int p = 0; p < ART_PART_COUNT; p++) {
        out[p] = NULL;
    }
    const ArtSet *set = art_set(species);
    if (set == NULL || action < 0 || action >= ACTION_COUNT) {
        return 0;
    }
    /* A covered part is in the frames already: no layer over it. */
    if (!(set->covers & ART_COVERS_ARMOUR)) {
        out[ART_PART_ARMOUR] = layer_for(set, ART_PART_ARMOUR, traits->armour, action);
    }
    if (!(set->covers & ART_COVERS_SWIM)) {
        out[ART_PART_SWIM] = layer_for(set, ART_PART_SWIM, traits->swim, action);
    }
    if (!(set->covers & ART_COVERS_WEAPON)) {
        out[ART_PART_WEAPON] = layer_for(set, ART_PART_WEAPON, traits->weapon, action);
    }
    int filled = 0;
    for (int p = 0; p < ART_PART_COUNT; p++) {
        filled += out[p] != NULL;  /* a comparison is an int, 0 or 1, in C */
    }
    return filled;
}

float art_scale_for(int species, int w, int h)
{
    const ArtSet *set = art_set(species);
    if (set == NULL || set->stand_w <= 0 || set->stand_h <= 0 || w <= 0 || h <= 0) {
        return 0.0f;
    }
    /* The casts to float before multiplying: the products are small here, but
     * float keeps the division from rounding to a whole number. */
    return sqrtf(((float)w * (float)h) / ((float)set->stand_w * (float)set->stand_h));
}

/* The art pixel's ink at (x, y), INK_NONE outside the frame. */
static uint8_t art_ink(const ArtFrame *f, int x, int y)
{
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) {
        return INK_NONE;
    }
    /* The casts: x and y were checked against w and h, so both are small and
     * not negative, and the index fits. */
    return f->ink[(size_t)y * (size_t)f->w + (size_t)x];
}

static bool inked_at(const SkeletonRaster *r, int x, int y)
{
    if (x < 0 || y < 0 || x >= r->w || y >= r->h) {
        return false;
    }
    return r->ink[y * r->w + x] != INK_NONE;
}

/* Whether a pixel of this ink turns into the outline at the silhouette's
 * edge: the fur's shades and the mane's (a mane is as thick as fur), but not
 * a horn or a tusk, which can be one or two pixels thin and would turn all
 * outline. */
static bool takes_outline(uint8_t ink)
{
    return (ink >= INK_DEEP && ink <= INK_BELLY) ||
           (ink >= INK_MANE0 && ink < INK_MANE0 + 5);
}

/* The grid pixel's sample (sx, sy) of `samples` each way, taken to frame f's
 * art pixel: the anchor pixel's middle sits on the grid anchor's. */
static uint8_t sample_ink(const ArtFrame *f, int gx, int gy, int sx, int sy, int samples,
                          float scale, float anchor_x, float anchor_y)
{
    float px = (float)gx + ((float)sx + 0.5f) / (float)samples;
    float py = (float)gy + ((float)sy + 0.5f) / (float)samples;
    float ax = (float)f->ax + 0.5f + (px - anchor_x - 0.5f) / scale;
    float ay = (float)f->ay + 0.5f + (py - anchor_y - 0.5f) / scale;
    return art_ink(f, (int)floorf(ax), (int)floorf(ay));
}

/* The PART_ value a layer's pixels are given, by ART_PART_ slot. */
static const uint8_t LAYER_PART[ART_PART_COUNT] = {
    [ART_PART_ARMOUR] = PART_SHELL, [ART_PART_SWIM] = PART_BODY,
    [ART_PART_WEAPON] = PART_HORN,
};

/* Draws one layer frame over the raster: every grid pixel any of whose
 * samples lands on a layer pixel takes the layer ink most of those samples
 * have. The inked box (*x0..*y1) grows to hold what is drawn. */
static void draw_layer(SkeletonRaster *r, const ArtFrame *f, uint8_t part, int samples,
                       float scale, float anchor_x, float anchor_y,
                       int *x0, int *y0, int *x1, int *y1)
{
    for (int gy = 0; gy < r->h; gy++) {
        for (int gx = 0; gx < r->w; gx++) {
            int count[INK_COUNT] = {0};
            int inked = 0;
            for (int sy = 0; sy < samples; sy++) {
                for (int sx = 0; sx < samples; sx++) {
                    uint8_t ink = sample_ink(f, gx, gy, sx, sy, samples, scale,
                                             anchor_x, anchor_y);
                    if (ink != INK_NONE && ink < INK_COUNT) {
                        count[ink]++;
                        inked++;
                    }
                }
            }
            if (inked == 0) {
                continue;
            }
            int best = INK_NONE;
            for (int k = 1; k < INK_COUNT; k++) {
                if (count[k] > count[best]) {
                    best = k;
                }
            }
            r->ink[gy * r->w + gx] = (uint8_t)best;
            r->part[gy * r->w + gx] = part;
            *x0 = gx < *x0 ? gx : *x0;
            *y0 = gy < *y0 ? gy : *y0;
            *x1 = gx + 1 > *x1 ? gx + 1 : *x1;
            *y1 = gy + 1 > *y1 ? gy + 1 : *y1;
        }
    }
}

void art_raster(SkeletonRaster *r, const ArtClip *clip, const ArtClip *const *layers,
                int frame, float scale, float anchor_x, float anchor_y)
{
    size_t cells = (size_t)r->w * (size_t)r->h;
    memset(r->ink, INK_NONE, cells);
    memset(r->part, PART_NONE, cells);
    r->x0 = r->y0 = r->x1 = r->y1 = 0;
    r->eye_x = r->eye_y = -1;
    r->eye_big = false;
    r->shapes = 0;
    r->body_dx = 0.0f;
    if (clip == NULL || clip->frames <= 0 || !(scale > 0.0f)) {
        return;
    }
    int n = clip->frames;
    const ArtFrame *f = &clip->frame[(frame % n + n) % n];

    /* Samples per grid pixel each way: one when the art is not shrunk, more
     * when one grid pixel covers several art pixels. */
    int samples = (int)ceilf(1.0f / scale);
    samples = samples < 1 ? 1 : (samples > ART_SAMPLES_MAX ? ART_SAMPLES_MAX : samples);
    int total = samples * samples;

    bool any = false;
    int x0 = r->w, y0 = r->h, x1 = 0, y1 = 0;
    for (int gy = 0; gy < r->h; gy++) {
        for (int gx = 0; gx < r->w; gx++) {
            /* How many samples landed on each ink. An array sized by the
             * enum's last value: a C array's length must be known when the
             * program is compiled, and INK_COUNT is. */
            int count[INK_COUNT] = {0};
            int inked = 0;
            for (int sy = 0; sy < samples; sy++) {
                for (int sx = 0; sx < samples; sx++) {
                    uint8_t ink = sample_ink(f, gx, gy, sx, sy, samples, scale,
                                             anchor_x, anchor_y);
                    if (ink != INK_NONE && ink < INK_COUNT) {
                        count[ink]++;
                        inked++;
                    }
                }
            }
            /* Fewer than a third of the samples inked: left empty. A third,
             * not a half, so a leg one art pixel wide survives a shrink to a
             * third (half lost the legs of the smallest wolves). */
            if (3 * inked < total || inked == 0) {
                continue;
            }
            int best = INK_NONE;
            for (int k = 1; k < INK_COUNT; k++) {
                if (count[k] > count[best]) {
                    best = k;
                }
            }
            /* The cast: best is an INK_ value, under INK_COUNT. */
            r->ink[gy * r->w + gx] = (uint8_t)best;
            r->part[gy * r->w + gx] = PART_BODY;
            any = true;
            x0 = gx < x0 ? gx : x0;
            y0 = gy < y0 ? gy : y0;
            x1 = gx + 1 > x1 ? gx + 1 : x1;
            y1 = gy + 1 > y1 ? gy + 1 : y1;
        }
    }
    /* The layers, in slot order, each its own frame of the same number. */
    for (int p = 0; layers != NULL && p < ART_PART_COUNT; p++) {
        const ArtClip *layer = layers[p];
        if (layer == NULL || layer->frames <= 0) {
            continue;
        }
        int ln = layer->frames;
        draw_layer(r, &layer->frame[(frame % ln + ln) % ln], LAYER_PART[p], samples, scale,
                   anchor_x, anchor_y, &x0, &y0, &x1, &y1);
        any = any || x1 > x0;
    }
    if (!any) {
        return;
    }
    /* The silhouette's edge becomes the outline: only fur and mane shades, so
     * a horn one or two pixels thin keeps its colour instead of turning all
     * outline.
     * Changing a pixel to the outline keeps it inked, so the test of a later
     * pixel's neighbours gives the same answer whichever order they are
     * visited in. */
    for (int gy = y0; gy < y1; gy++) {
        for (int gx = x0; gx < x1; gx++) {
            uint8_t here = r->ink[gy * r->w + gx];
            if (takes_outline(here) &&
                (!inked_at(r, gx - 1, gy) || !inked_at(r, gx + 1, gy) ||
                 !inked_at(r, gx, gy - 1) || !inked_at(r, gx, gy + 1))) {
                r->ink[gy * r->w + gx] = INK_OUTLINE;
            }
        }
    }
    /* The rim: light under a top edge, shadow over a bottom one. Only base
     * pixels change and only into light or shadow, so no pixel's test is
     * changed by an earlier one. */
    for (int gy = y0; gy < y1; gy++) {
        for (int gx = x0; gx < x1; gx++) {
            uint8_t *ink = &r->ink[gy * r->w + gx];
            if (*ink != INK_BASE) {
                continue;
            }
            if (gy > 0 && r->ink[(gy - 1) * r->w + gx] == INK_OUTLINE) {
                *ink = INK_LIGHT;
            } else if (gy + 1 < r->h && r->ink[(gy + 1) * r->w + gx] == INK_OUTLINE) {
                *ink = INK_SHADOW;
            }
        }
    }
    r->x0 = x0;
    r->y0 = y0;
    r->x1 = x1;
    r->y1 = y1;
    if (f->ex >= 0 && f->ey >= 0) {
        /* The eye pixel's middle, taken to the grid the way the samples were. */
        float gx = anchor_x + 0.5f + ((float)f->ex + 0.5f - (float)f->ax - 0.5f) * scale;
        float gy = anchor_y + 0.5f + ((float)f->ey + 0.5f - (float)f->ay - 0.5f) * scale;
        int ex = (int)floorf(gx), ey = (int)floorf(gy);
        if (inked_at(r, ex, ey)) {
            r->eye_x = ex;
            r->eye_y = ey;
        }
    }
}
