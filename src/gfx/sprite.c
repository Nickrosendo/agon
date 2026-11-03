#include "sprite.h"

#include <math.h>   /* fabsf, floorf */
#include <stdlib.h> /* calloc, free */
#include <string.h> /* memset */

/* How many steps each trait is rounded into. */
#define SIZE_STEPS 4
#define SIGHT_STEPS 4
/* How far down the tile the belly reaches; the legs fill the rest. */
#define BELLY 0.80f
/* Sight from here up puts the eye on a stalk. */
#define STALK_FROM 0.7f

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
    int leg_count;
    int leg_column[6];       /* whole pixels: legs stay crisp */
    bool spots, teeth, open_mouth, pinched;
    float pinch_x;
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
    BodyTraits t = {g->g[GENE_SIZE], g->g[GENE_SPEED], g->g[GENE_SIGHT],
                    g->g[GENE_PLANT], g->g[GENE_MEAT]};
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

/* Legs on whole pixel columns, at least two apart so each can be told from
 * the next, spread along the body and swinging while walking. */
static void place_legs(Shape *s, int frame)
{
    int n = s->leg_count;
    float wanted = s->rx * 1.5f * (float)s->size;
    int gap = n > 1 ? (int)(wanted / (float)(n - 1) + 0.5f) : 0;
    gap = gap < 2 ? 2 : gap;
    int span = gap * (n - 1);
    int first = (int)(s->cx * (float)s->size) - span / 2;
    /* A wide spread of six legs can pass the tile's edge: slide it back. */
    first = first < 0 ? 0 : first;
    if (first + span > s->size - 1) {
        first = s->size - 1 - span;
    }
    int swing = 0;
    if (frame == FRAME_WALK1) {
        swing = 1;
    } else if (frame == FRAME_WALK3) {
        swing = -1;
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

static void make_shape(Shape *s, const BodyTraits *t, int size, int frame)
{
    float body_size = round_to_steps(t->size, SIZE_STEPS);
    float sight = round_to_steps(t->sight, SIGHT_STEPS);
    float meat = t->meat >= 0.5f ? 1.0f : 0.0f;
    float px = 1.0f / (float)size;

    memset(s, 0, sizeof(*s));
    s->size = size;
    s->px = px;
    s->line = px * 0.5f;
    s->leg_count = leg_count(t->speed, size);
    s->rx = 0.19f + 0.20f * body_size;
    s->ry = 0.13f + 0.15f * body_size;
    /* The belly sits at the same height whatever the size, so legs stay short
     * and a small creature stands on the ground instead of on stilts. The
     * body bobs by a pixel. */
    s->cx = 0.42f;
    s->cy = BELLY - s->ry - (float)frame_bob(frame) * px;
    /* Biting: the head lunges forward by a pixel or two. */
    float lunge = frame == FRAME_BITE ? px * (size > 8 ? 2.0f : 1.0f) : 0.0f;
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
        if (p.i == s->leg_column[k]) {
            return true;
        }
    }
    return false;
}

static bool part_tail(const Shape *s, Point p)
{
    float tail_x = s->cx - s->rx - (s->cy - p.v) * 0.5f;
    return p.v > s->cy - s->ry * 1.1f && p.v < s->cy - s->ry * 0.1f &&
           fabsf(p.u - tail_x) < s->line + 0.01f;
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
    {part_legs, PAINT_BODY},  {part_tail, PAINT_BODY},
    {part_body, PAINT_BODY},  {part_spots, PAINT_WHITE},
    {part_stalk, PAINT_BODY}, {part_head, PAINT_BODY},
    {part_mouth, PAINT_HOLE}, {part_teeth, PAINT_WHITE},
    {part_eye, PAINT_HOLE},
};
#define PART_COUNT ((int)(sizeof(PARTS) / sizeof(PARTS[0])))

void sprite_build(SpriteMask *out, const BodyTraits *t, int size, int frame)
{
    Shape s;
    make_shape(&s, t, size, frame);
    memset(out, 0, sizeof(*out));
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            Point p = {((float)i + 0.5f) * s.px, ((float)j + 0.5f) * s.px, i, j};
            /* The last part to cover the pixel decides it. */
            int paint = PAINT_HOLE;
            for (int k = 0; k < PART_COUNT; k++) {
                if (PARTS[k].covers(&s, p)) {
                    paint = PARTS[k].paint;
                }
            }
            /* 1u, not 1: shifts on unsigned values are always defined. The
             * cast back is safe because size is at most 16. */
            uint16_t bit = (uint16_t)(1u << (size - 1 - i));
            if (paint == PAINT_BODY) {
                out->body[j] = (uint16_t)(out->body[j] | bit);
            } else if (paint == PAINT_WHITE) {
                out->white[j] = (uint16_t)(out->white[j] | bit);
            }
        }
    }
}

void sprite_eye(const BodyTraits *t, int size, int frame, int *column, int *row)
{
    Shape s;
    make_shape(&s, t, size, frame);
    *column = (int)(s.ex * (float)size);
    *row = (int)(s.ey * (float)size);
}

bool sprite_cache_init(SpriteCache *c, int capacity)
{
    /* calloc: a block of zeros, so every slot starts with ready false. */
    c->slots = calloc((size_t)capacity, sizeof(*c->slots));
    c->capacity = c->slots != NULL ? capacity : 0;
    return c->slots != NULL;
}

void sprite_cache_free(SpriteCache *c)
{
    free(c->slots);
    *c = (SpriteCache){0};
}

void sprite_cache_reset(SpriteCache *c)
{
    for (int i = 0; i < c->capacity; i++) {
        c->slots[i].ready = false;
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
