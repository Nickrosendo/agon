#include "sprite.h"

#include <math.h>   /* fabsf, floorf, sqrtf */
#include <stdlib.h> /* calloc, free */
#include <string.h> /* memset */

/* How many steps each trait is rounded into. */
#define SIZE_STEPS 4
#define SIGHT_STEPS 4
/* How far down the tile the belly reaches; the legs fill the rest. */
#define BELLY 0.80f
/* Long legs raise the belly to here. */
#define BELLY_LONG_LEGS 0.66f
/* Sight from here up puts the eye on a stalk. */
#define STALK_FROM 0.7f
/* A part gene from here up shows its part; a weapon from WEAPON_HORN up shows
 * a horn as well as the tusk. Steps, like the traits: a small mutation leaves
 * the picture alone. */
#define PART_FROM 0.5f
#define WEAPON_HORN 0.8f

/* Everything the parts need to decide their pixels. Lengths are shares of
 * the tile: 0..1 across, whatever the pixel size. */
typedef struct {
    int size;
    float px;                /* one pixel, as a share of the tile */
    float cx, cy, rx, ry;    /* the body: an ellipse */
    float hx, hy, hr;        /* the head: a circle */
    float ex, ey, eye_r;     /* the eye: a hole */
    float stalk;             /* how far up the eye stands; 0 for no stalk */
    float line;              /* half the width of a leg, tail or stalk */
    int thick;               /* a leg's width in whole pixels: wider when large */
    int leg_count;
    int leg_column[6];       /* whole pixels: legs stay crisp */
    bool spots, teeth, open_mouth, pinched;
    float pinch_x;
    bool plates, tusk, horn, fins; /* the parts from genes */
} Shape;

/* What one sampled pixel is. */
typedef struct {
    float u, v; /* its middle, as shares of the tile */
    int i, j;   /* its column and row */
} Point;

/* What a part does to the pixels it covers. */
enum { PAINT_BODY, PAINT_WHITE, PAINT_HOLE };

/* A trait cut into steps, as 0..1: 4 steps gives 0, 1/3, 2/3 or 1. */
static float round_to_steps(float value, int steps)
{
    /* Before the cast below: converting a float too large for int is undefined,
     * and a trait built from a body's real size has no top. */
    if (value >= 1.0f) {
        return 1.0f;
    }
    int step = (int)(value * (float)steps); /* a cast drops the fraction */
    step = step > steps - 1 ? steps - 1 : step;
    step = step < 0 ? 0 : step;
    return (float)step / (float)(steps - 1);
}

BodyTraits sprite_traits(const Genome *g)
{
    /* The body shows whichever plant food it digests better. */
    float leaf = g->g[GENE_LEAF];
    float fruit = g->g[GENE_FRUIT];
    BodyTraits t = {g->g[GENE_SIZE], g->g[GENE_SPEED], g->g[GENE_SIGHT],
                    leaf > fruit ? leaf : fruit, g->g[GENE_MEAT],
                    g->g[GENE_ARMOUR], g->g[GENE_WEAPON], g->g[GENE_LEGS],
                    g->g[GENE_SWIM]};
    return t;
}

static int leg_count(float speed, int size)
{
    int legs = speed < 1.0f / 3.0f ? 2 : (speed < 2.0f / 3.0f ? 4 : 6);
    /* Eight pixels have no room for six separate legs. */
    if (size < 16) {
        legs = legs == 2 ? 2 : (legs == 4 ? 3 : 4);
    }
    return legs;
}

/* Legs on whole pixel columns, each thick pixels wide and at least its own
 * width apart so each can be told from the next, spread along the body and
 * swinging while walking. A leg covers leg_column[k] and the thick - 1
 * columns to its right. */
static void place_legs(Shape *s, int frame)
{
    int n = s->leg_count;
    int thick = s->thick;
    float wanted = s->rx * 1.5f * (float)s->size;
    int gap = n > 1 ? (int)(wanted / (float)(n - 1) + 0.5f) : 0;
    gap = gap < 2 * thick ? 2 * thick : gap;
    int span = gap * (n - 1) + thick - 1; /* first leg's left to last leg's right */
    int first = (int)(s->cx * (float)s->size) - span / 2;
    /* A wide spread of six legs can pass the tile's edge: slide it back. A
     * swing moves a leg by thick, so keep that much room at both ends. */
    first = first < thick ? thick : first;
    if (first + span > s->size - 1 - thick) {
        first = s->size - 1 - thick - span;
    }
    int swing = 0;
    if (frame == FRAME_WALK1) {
        swing = thick;
    } else if (frame == FRAME_WALK3) {
        swing = -thick;
    }
    for (int k = 0; k < n; k++) {
        /* k % 2 alternates, so neighbouring legs swing opposite ways. */
        int c = first + k * gap + (k % 2 == 0 ? swing : -swing);
        s->leg_column[k] = c;
    }
}

/* Whether this frame lifts the body by a pixel. */
static int frame_bob(int frame)
{
    switch (frame) {
    case FRAME_WALK1: case FRAME_WALK3: case FRAME_REST1:
        return 1;
    default:
        return 0;
    }
}

/* Snaps a length to the middle of the pixel it falls in, so a small hole
 * always covers a whole pixel, never the corner of four. */
static float pixel_middle(float at, int size)
{
    float column = floorf(at * (float)size);
    return (column + 0.5f) / (float)size;
}

/* wide: a picture several tiles across, which is always a large body. */
static void make_shape(Shape *s, const BodyTraits *t, int size, int frame,
                       bool wide)
{
    float body_size = wide ? 1.0f : round_to_steps(t->size, SIZE_STEPS);
    float sight = round_to_steps(t->sight, SIGHT_STEPS);
    float meat = t->meat >= 0.5f ? 1.0f : 0.0f;
    float px = 1.0f / (float)size;
    /* Eight pixels have no room for parts. */
    bool parts = size > 8;

    memset(s, 0, sizeof(*s));
    s->size = size;
    s->px = px;
    /* A 48-pixel picture drawn with 1-pixel legs would look like a spider:
     * lines get thicker as pictures get larger, 1 pixel per 16. */
    s->thick = size / 16 < 1 ? 1 : size / 16;
    s->line = px * 0.5f * (float)s->thick;
    s->leg_count = leg_count(t->speed, size);
    s->rx = 0.19f + 0.20f * body_size;
    s->ry = 0.13f + 0.15f * body_size;
    s->plates = parts && t->armour >= PART_FROM;
    s->tusk = parts && t->weapon >= PART_FROM;
    s->horn = parts && t->weapon >= WEAPON_HORN;
    s->fins = parts && t->swim >= PART_FROM;
    bool long_legs = parts && t->legs >= PART_FROM;
    /* The belly sits at the same height whatever the size, so legs stay short
     * and a small creature stands on the ground instead of on stilts; long
     * legs raise it. The body bobs by a pixel (or a line's width). */
    s->cx = 0.42f;
    s->cy = (long_legs ? BELLY_LONG_LEGS : BELLY) - s->ry -
            (float)(frame_bob(frame) * s->thick) * px;
    /* Biting: the head lunges forward by a pixel or two. */
    float lunge = frame == FRAME_BITE
                      ? px * (size > 8 ? 2.0f : 1.0f) * (float)s->thick
                      : 0.0f;
    s->hx = s->cx + s->rx * 0.9f + lunge;
    s->hy = s->cy - s->ry * 0.5f;
    s->hr = 0.11f + 0.07f * body_size + 0.05f * meat;

    s->stalk = sight >= STALK_FROM ? 0.13f : 0.0f;
    s->eye_r = 0.03f + 0.05f * sight;
    s->eye_r = s->eye_r < px * 0.62f ? px * 0.62f : s->eye_r;
    s->ex = pixel_middle(s->hx + s->hr * 0.25f, size);
    s->ey = pixel_middle(s->hy - s->hr * 0.2f - s->stalk, size);

    s->spots = t->plant >= 0.5f && size > 8;
    s->teeth = meat > 0.0f;
    s->open_mouth = frame == FRAME_BITE;
    /* The back fin would hide behind plates, so an armoured swimmer shows the
     * plates and keeps only its fin tail. */
    /* Wishing to mate: the body stretches and pinches in at the middle. */
    if (frame == FRAME_MATE) {
        s->pinched = true;
        s->rx *= 1.15f;
        s->pinch_x = s->cx;
    }
    place_legs(s, frame);
}

/* The parts. Each says whether it covers a pixel. */

static bool part_legs(const Shape *s, Point p)
{
    if (p.v <= s->cy + s->ry * 0.4f) {
        return false; /* above the legs' top */
    }
    for (int k = 0; k < s->leg_count; k++) {
        if (p.i >= s->leg_column[k] && p.i < s->leg_column[k] + s->thick) {
            return true;
        }
    }
    return false;
}

/* Whether p is within half_width of the straight line from (x0, y0) to
 * (x1, y1): the share t along it of the point nearest p, kept between the
 * ends, then the distance to that point. */
static bool near_segment(Point p, float x0, float y0, float x1, float y1,
                         float half_width)
{
    float dx = x1 - x0, dy = y1 - y0;
    float t = ((p.u - x0) * dx + (p.v - y0) * dy) / (dx * dx + dy * dy);
    t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    float ex = p.u - (x0 + t * dx), ey = p.v - (y0 + t * dy);
    return ex * ex + ey * ey < half_width * half_width;
}

/* The top of the body's ellipse above p, or -1 where p is not over the body:
 * cy - ry * sqrt(1 - a * a), with a how far along the body p is. */
static float body_top(const Shape *s, Point p)
{
    float a = (p.u - s->cx) / s->rx;
    if (a <= -1.0f || a >= 1.0f) {
        return -1.0f;
    }
    return s->cy - s->ry * sqrtf(1.0f - a * a);
}

static bool part_tail(const Shape *s, Point p)
{
    if (s->fins) {
        /* A swimmer's tail: a forked fin that widens away from the body. */
        float d = (s->cx - s->rx * 0.8f) - p.u;
        if (d <= 0.0f || d >= s->rx * 0.55f) {
            return false;
        }
        float from_middle = fabsf(p.v - s->cy);
        bool forked = d > s->rx * 0.3f && from_middle < (d - s->rx * 0.3f) * 1.1f;
        return from_middle < d * 1.1f && !forked;
    }
    float tail_x = s->cx - s->rx - (s->cy - p.v) * 0.5f;
    return p.v > s->cy - s->ry * 1.1f && p.v < s->cy - s->ry * 0.1f &&
           fabsf(p.u - tail_x) < s->line + 0.01f;
}

static bool part_plates(const Shape *s, Point p)
{
    if (!s->plates) {
        return false;
    }
    float a = (p.u - s->cx) / s->rx;
    float top = body_top(s, p);
    if (top < 0.0f || fabsf(a) > 0.75f) {
        return false;
    }
    /* A row of points along the back, each rising to plate_h and falling: the
     * distance from the middle of each repeat. + 9 keeps it positive. */
    float repeat = a * 2.5f + 9.0f;
    float rise = 1.0f - 2.0f * fabsf(repeat - floorf(repeat) - 0.5f);
    float height = s->ry * 0.55f * rise;
    return height > s->px * 0.5f && p.v < top + s->px && p.v > top - height;
}

static bool part_back_fin(const Shape *s, Point p)
{
    if (!s->fins || s->plates) {
        return false;
    }
    float fin_x = s->cx - s->rx * 0.1f;
    float d = fabsf(p.u - fin_x) / (s->rx * 0.35f);
    return d < 1.0f && p.v < s->cy &&
           p.v > s->cy - s->ry - s->ry * 0.7f * (1.0f - d);
}

static bool part_body(const Shape *s, Point p)
{
    float a = (p.u - s->cx) / s->rx;
    float b = (p.v - s->cy) / s->ry;
    if (a * a + b * b >= 1.0f) {
        return false;
    }
    /* The waist of a body wishing to mate: a gap two pixels wide, top and bottom. */
    return !(s->pinched && fabsf(p.u - s->pinch_x) < s->px &&
             fabsf(b) > 0.45f);
}

static bool part_spots(const Shape *s, Point p)
{
    if (!s->spots) {
        return false;
    }
    float along = (p.u - s->cx) / s->rx * 2.2f;
    float up = (p.v - s->cy) / s->ry;
    float inside = ((p.u - s->cx) / s->rx) * ((p.u - s->cx) / s->rx) + up * up;
    if (inside >= 1.0f || up >= 0.0f || up <= -0.8f) {
        return false; /* only on the back of the body */
    }
    /* A repeating band across the back: + 9 keeps the sum positive before
     * floorf takes the fraction. */
    float repeat = along + 9.0f;
    return fabsf(repeat - floorf(repeat) - 0.5f) < 0.14f;
}

static bool part_stalk(const Shape *s, Point p)
{
    if (s->stalk <= 0.0f) {
        return false;
    }
    float dx = p.u - s->ex, dy = p.v - s->ey;
    /* The bulb that holds the eye, one pixel wider than the eye itself. */
    float bulb = s->eye_r + s->px;
    if (dx * dx + dy * dy < bulb * bulb) {
        return true;
    }
    return fabsf(p.u - s->ex) < s->line + 0.001f && p.v < s->hy && p.v > s->ey;
}

static bool part_head(const Shape *s, Point p)
{
    float dx = p.u - s->hx, dy = p.v - s->hy;
    return dx * dx + dy * dy < s->hr * s->hr;
}

static bool part_teeth(const Shape *s, Point p)
{
    return s->teeth && p.u > s->hx && p.v > s->hy + s->hr * 0.1f &&
           p.v < s->hy + s->hr * 0.9f && p.u < s->hx + s->hr + 0.04f &&
           (p.i + p.j) % 2 == 0;
}

static bool part_mouth(const Shape *s, Point p)
{
    /* The gap of an open mouth: a wedge at the front of the head. */
    return s->open_mouth && p.u > s->hx + s->hr * 0.15f &&
           fabsf(p.v - (s->hy + s->hr * 0.35f)) < s->px * 0.75f;
}

/* Weapons: a tusk down and forward from the mouth, and a horn back and up
 * from the top of the head. */
static bool part_tusk(const Shape *s, Point p)
{
    return s->tusk &&
           near_segment(p, s->hx + s->hr * 0.35f, s->hy + s->hr * 0.55f,
                        s->hx + s->hr * 0.95f, s->hy + s->hr * 1.25f,
                        s->px * 0.6f * (float)s->thick);
}

static bool part_horn(const Shape *s, Point p)
{
    return s->horn &&
           near_segment(p, s->hx - s->hr * 0.2f, s->hy - s->hr * 0.8f,
                        s->hx - s->hr * 0.6f, s->hy - s->hr * 1.4f,
                        s->px * 0.6f * (float)s->thick);
}

static bool part_eye(const Shape *s, Point p)
{
    float dx = p.u - s->ex, dy = p.v - s->ey;
    return dx * dx + dy * dy < s->eye_r * s->eye_r;
}

/*
 * The parts in painting order, later ones over earlier ones. A pointer to a
 * function is a value like any other, so a table of them is an ordinary
 * array. Adding fins or wings means adding a function and a line here.
 */
typedef struct {
    bool (*covers)(const Shape *s, Point p);
    int paint;
} Part;

static const Part PARTS[] = {
    {part_legs, PAINT_BODY},   {part_tail, PAINT_BODY},
    {part_plates, PAINT_WHITE}, {part_body, PAINT_BODY},
    {part_back_fin, PAINT_BODY}, {part_spots, PAINT_WHITE},
    {part_stalk, PAINT_BODY},  {part_head, PAINT_BODY},
    {part_mouth, PAINT_HOLE},  {part_teeth, PAINT_WHITE},
    {part_tusk, PAINT_WHITE},  {part_horn, PAINT_WHITE},
    {part_eye, PAINT_HOLE},
};
#define PART_COUNT ((int)(sizeof(PARTS) / sizeof(PARTS[0])))

/*
 * Samples every pixel of a size by size picture into 64-bit rows. touched
 * gets every pixel any part covered, holes included, for the outline. The
 * caller zeroes the rows first.
 */
static void build_rows(uint64_t *body, uint64_t *white, uint64_t *touched,
                       const BodyTraits *t, int size, int frame, bool wide)
{
    Shape s;
    make_shape(&s, t, size, frame, wide);
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            Point p = {((float)i + 0.5f) * s.px, ((float)j + 0.5f) * s.px, i, j};
            /* The last part to cover the pixel decides it. */
            int paint = PAINT_HOLE;
            bool any = false;
            for (int k = 0; k < PART_COUNT; k++) {
                if (PARTS[k].covers(&s, p)) {
                    paint = PARTS[k].paint;
                    any = true;
                }
            }
            /* 1ull: a 64-bit 1. A plain 1 is an int, and shifting it by 32 or
             * more is undefined behaviour. */
            uint64_t bit = 1ull << (size - 1 - i);
            if (paint == PAINT_BODY) {
                body[j] |= bit;
            } else if (paint == PAINT_WHITE) {
                white[j] |= bit;
            }
            if (any) {
                touched[j] |= bit;
            }
        }
    }
}

void sprite_build(SpriteMask *out, const BodyTraits *t, int size, int frame)
{
    uint64_t body[16] = {0}, white[16] = {0}, touched[16] = {0};
    build_rows(body, white, touched, t, size, frame, false);
    for (int j = 0; j < 16; j++) {
        /* Safe narrowing: size is at most 16, so no bit above bit 15 is set. */
        out->body[j] = (uint16_t)body[j];
        out->white[j] = (uint16_t)white[j];
    }
}

void sprite_build_wide(WideMask *out, const BodyTraits *t, int size, int frame)
{
    memset(out, 0, sizeof(*out));
    size = size > SPRITE_MAX_COLS ? SPRITE_MAX_COLS : size;
    out->size = size;
    uint64_t touched[SPRITE_MAX_COLS] = {0};
    build_rows(out->body, out->white, touched, t, size, frame, true);
    /* The low size bits: what is inside the picture. */
    uint64_t inside = size == 64 ? ~0ull : (1ull << size) - 1u;
    for (int j = 0; j < size; j++) {
        /* Grown by a pixel left and right in this row, then the rows above
         * and below added. Shifting a row moves every pixel one column. */
        uint64_t grown = 0;
        for (int k = j - 1; k <= j + 1; k++) {
            if (k >= 0 && k < size) {
                grown |= touched[k] | (touched[k] << 1) | (touched[k] >> 1);
            }
        }
        out->outline[j] = grown & inside;
    }
}

int sprite_tiles_for(float size)
{
    /* Written as "not at least", so a NaN, for which every comparison is
     * false, falls into the first case. */
    if (!(size >= SPRITE_TWO_TILES)) {
        return 1;
    }
    if (!(size >= SPRITE_THREE_TILES)) {
        return 2;
    }
    return SPRITE_MAX_TILES;
}

/* The scars' places, as shares of the body's extent: middle across, middle
 * down. */
static const float SCAR_AT[3][2] = {{0.45f, 0.40f}, {0.65f, 0.55f}, {0.28f, 0.60f}};

void sprite_scars(uint64_t *out, const uint64_t *body, int size, int count)
{
    memset(out, 0, (size_t)size * sizeof(out[0]));
    count = count > 3 ? 3 : count;
    /* The body's extent: its first and last rows and columns. */
    int top = -1, bottom = -1, left = size, right = -1;
    for (int j = 0; j < size; j++) {
        if (body[j] == 0) {
            continue;
        }
        top = top < 0 ? j : top;
        bottom = j;
        for (int i = 0; i < size; i++) {
            if ((body[j] >> (size - 1 - i)) & 1u) {
                left = i < left ? i : left;
                right = i > right ? i : right;
            }
        }
    }
    if (top < 0 || count <= 0) {
        return;
    }
    int w = right - left + 1, h = bottom - top + 1;
    int length = w / 4 < 3 ? 3 : w / 4;
    int thick = size / 16 < 1 ? 1 : size / 16;
    for (int k = 0; k < count; k++) {
        int mid_i = left + (int)(SCAR_AT[k][0] * (float)w);
        int mid_j = top + (int)(SCAR_AT[k][1] * (float)h);
        /* A cut slanting down to the right, thick pixels wide. */
        for (int d = 0; d < length; d++) {
            int j = mid_j - length / 2 + d;
            for (int e = 0; e < thick; e++) {
                int i = mid_i - length / 2 + d + e;
                if (j >= 0 && j < size && i >= 0 && i < size) {
                    out[j] |= 1ull << (size - 1 - i);
                }
            }
        }
    }
    for (int j = 0; j < size; j++) {
        out[j] &= body[j];
    }
}

void sprite_eye(const BodyTraits *t, int size, int frame, int *column, int *row)
{
    Shape s;
    make_shape(&s, t, size, frame, false);
    *column = (int)(s.ex * (float)size);
    *row = (int)(s.ey * (float)size);
}

bool sprite_cache_init(SpriteCache *c, int capacity)
{
    *c = (SpriteCache){0};
    /* calloc: a block of zeros, so every slot starts with ready false and
     * every large entry with tiles 0, unused. */
    c->slots = calloc((size_t)capacity, sizeof(*c->slots));
    c->big = calloc(SPRITE_BIG_ENTRIES, sizeof(*c->big));
    if (c->slots == NULL || c->big == NULL) {
        /* free(NULL) does nothing, so freeing both is safe whichever failed. */
        free(c->slots);
        free(c->big);
        *c = (SpriteCache){0};
        return false;
    }
    c->capacity = capacity;
    return true;
}

void sprite_cache_free(SpriteCache *c)
{
    free(c->slots);
    free(c->big);
    *c = (SpriteCache){0};
}

void sprite_cache_reset(SpriteCache *c)
{
    for (int i = 0; i < c->capacity; i++) {
        c->slots[i].ready = false;
    }
    if (c->big != NULL) {
        for (int i = 0; i < SPRITE_BIG_ENTRIES; i++) {
            c->big[i].tiles = 0;
        }
    }
}

const SpriteMask *sprite_get(SpriteCache *c, int slot, uint32_t id,
                             const Genome *g, int size, int frame)
{
    if (slot < 0 || slot >= c->capacity) {
        return NULL;
    }
    SpriteSlot *s = &c->slots[slot];
    if (!s->ready || s->id != id) {
        BodyTraits t = sprite_traits(g);
        for (int f = 0; f < FRAME_COUNT; f++) {
            sprite_build(&s->large[f], &t, 16, f);
            sprite_build(&s->small[f], &t, 8, f);
        }
        s->id = id;
        s->ready = true;
    }
    return size == 16 ? &s->large[frame] : &s->small[frame];
}

const WideMask *sprite_get_wide(SpriteCache *c, uint32_t id, const Genome *g,
                                int tiles, int base, int frame)
{
    if (c->big == NULL || tiles < 2 || tiles > SPRITE_MAX_TILES ||
        (base != 16 && base != 8) || frame < 0 || frame >= FRAME_COUNT) {
        return NULL;
    }
    /* Unsigned: after 4 billion lookups it goes back to 0, which only makes
     * one choice of entry to reuse a poor one. */
    c->clock++;
    BigEntry *e = NULL;
    BigEntry *oldest = &c->big[0];
    for (int i = 0; i < SPRITE_BIG_ENTRIES; i++) {
        BigEntry *b = &c->big[i];
        if (b->tiles == tiles && b->base == base && b->id == id) {
            e = b;
            break;
        }
        /* An unused entry is taken first: it counts as the oldest. */
        bool older = b->tiles == 0 ||
                     (oldest->tiles != 0 && b->last_used < oldest->last_used);
        if (older) {
            oldest = b;
        }
    }
    if (e == NULL) {
        e = oldest;
        e->id = id;
        e->tiles = tiles;
        e->base = base;
        for (int f = 0; f < FRAME_COUNT; f++) {
            e->ready[f] = false;
        }
    }
    e->last_used = c->clock;
    if (!e->ready[frame]) {
        BodyTraits t = sprite_traits(g);
        sprite_build_wide(&e->frames[frame], &t, base * tiles, frame);
        e->ready[frame] = true;
    }
    return &e->frames[frame];
}
