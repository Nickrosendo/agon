#include "art.h"

#include <math.h>   /* ceilf, floorf, sqrtf */
#include <string.h> /* memset */

/* The most samples taken across one grid pixel, each way: enough for a
 * picture shrunk to an eighth. */
#define ART_SAMPLES_MAX 8

static const ArtSet *set_of(int species)
{
    for (int i = 0; i < ART_SET_COUNT; i++) {
        if (ART_SETS[i].species == species) {
            return &ART_SETS[i];
        }
    }
    return NULL;
}

/* Whether the genes show a part the frames have no place for: plates, a tusk
 * or horn, a fin. Long legs are left out on purpose: they only lengthen the
 * legs the frames already have, and most hunters carry the gene (the lab's
 * hunter has legs 0.5, rounded to 2/3). */
static bool shows_a_part(const BodyTraits *t)
{
    return t->armour >= PART_FROM || t->weapon >= PART_FROM || t->swim >= PART_FROM;
}

const ArtClip *art_clip(int species, int action, const BodyTraits *traits)
{
    const ArtSet *set = set_of(species);
    if (set == NULL || action < 0 || action >= ACTION_COUNT || shows_a_part(traits)) {
        return NULL;
    }
    const ArtClip *clip = &set->clip[action];
    return clip->frames > 0 ? clip : NULL;
}

float art_scale_for(int species, int w, int h)
{
    const ArtSet *set = set_of(species);
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

void art_raster(SkeletonRaster *r, const ArtClip *clip, int frame, float scale,
                float anchor_x, float anchor_y)
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
                    /* The sample's place on the grid, then on the art: the
                     * anchor pixel's middle sits on the grid anchor's. */
                    float px = (float)gx + ((float)sx + 0.5f) / (float)samples;
                    float py = (float)gy + ((float)sy + 0.5f) / (float)samples;
                    float ax = (float)f->ax + 0.5f + (px - anchor_x - 0.5f) / scale;
                    float ay = (float)f->ay + 0.5f + (py - anchor_y - 0.5f) / scale;
                    uint8_t ink = art_ink(f, (int)floorf(ax), (int)floorf(ay));
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
    if (!any) {
        return;
    }
    /* The silhouette's edge becomes the outline. Changing a pixel to the
     * outline keeps it inked, so the test of a later pixel's neighbours gives
     * the same answer whichever order they are visited in. */
    for (int gy = y0; gy < y1; gy++) {
        for (int gx = x0; gx < x1; gx++) {
            if (inked_at(r, gx, gy) &&
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
