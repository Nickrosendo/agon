#include "skeleton.h"

#include <math.h>   /* sqrtf, cosf, sinf, floorf, fabsf, fmaxf */
#include <string.h> /* memset */

#define PI 3.14159265f
#define TWO_PI 6.2831853f

/* The light comes from the top left and a little in front (v9:131), as a unit
 * vector: each part divided by the length sqrt(0.45² + 0.8² + 0.55²). A static
 * const may be set from arithmetic on constants, but not from a call such as
 * sqrtf, so the length is written out. */
#define LIGHT_LENGTH 1.0700467f
static const float LIGHT_X = -0.45f / LIGHT_LENGTH;
static const float LIGHT_Y = -0.8f / LIGHT_LENGTH;
static const float LIGHT_Z = 0.55f / LIGHT_LENGTH;

/* The most points a tube may bend through: the ringed tail has 9. */
#define TUBE_POINTS 9
#define SHAPE_OVALS 3

/* Where a pixel falls inside a shape: the surface's slope there (nx, ny, each
 * -1..1, 0, 0 at the middle), how deep (d: 0 at the middle, 1 at the edge),
 * and how far along a tube (seg: segment number plus the share of the way
 * along it). */
typedef struct {
    float nx, ny, d, seg;
} Hit;

typedef struct {
    float cx, cy, rx, ry;
} Oval;

/* Flags a shape can carry, bits of one number. */
enum {
    SHAPE_INNER = 1u << 0, /* the inside of an ear: belly colour */
    SHAPE_DARK = 1u << 1,  /* the tassel at a tail's end: the darkest shade */
    SHAPE_FLAT = 1u << 2   /* a flat flap (the elephant's ear): softer shading */
};

/*
 * One shape: up to three ovals and a tube (a tapered line through points,
 * radius r0 at the first point to r1 at the last), or a triangle through the
 * first three points. A pixel is inside when it is inside any of them; the
 * deepest one gives its slope, so where the ovals of the body overlap there is
 * no seam (v9:305). gx0..gx1, gy0..gy1 is the box of grid pixels it can touch.
 */
typedef struct {
    int part, bias, leg; /* leg: 0 upper, 1 lower, 2 foot, -1 not a leg */
    unsigned flags;
    int ovals;
    Oval oval[SHAPE_OVALS];
    int points;
    float pt[TUBE_POINTS][2];
    float r0, r1;
    bool triangle;
    int gx0, gy0, gx1, gy1;
} Shape;

/* A box of grid pixels, both corners included. */
typedef struct {
    int x0, y0, x1, y1;
} Box;

/* The shapes of one body, in painting order. When the list is full, a new
 * shape goes into spare, which is never painted: a body that needs too many
 * shapes loses its last ones rather than writing past the array. box holds
 * every pixel a shape may touch, grown by one for the outline: the passes
 * after painting look only there, as nothing outside it is ever inked. */
typedef struct {
    Shape shape[SKELETON_SHAPES_MAX];
    int count;
    Shape spare;
    Box box;
} ShapeList;

/* Everything about one body in one pose, in the mockup's units: pixels with the
 * ground point at 0, 0, y growing downwards. The pose's values in units of K
 * are multiplied out here. */
typedef struct {
    const Anatomy *a;
    const Species *sp;
    const SpritePose *p;
    float k, bob, lower, stride, lift;
    float leg_len, hip_r, chest_r, body_len;
    float bx, by, hip_x, chest_x, chest_y;
    float neck_angle, nbx, nby, hx, hy, md, mx, my;
    float jaw, jpx, jpy, jtx, jty;
    float hip[4][2], knee[4][2], foot[4][2];
    int w, h, ox, oy;
} Body;

static float lerp(float a, float b, float u)
{
    return a + (b - a) * u;
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* v9:160: slow at both ends. */
static float ease_in_out(float u)
{
    if (u < 0.5f) {
        return 2.0f * u * u;
    }
    float v = -2.0f * u + 2.0f;
    return 1.0f - v * v * 0.5f;
}

/* ---- hit tests (v9:132-153) ---- */

static bool hit_oval(const Oval *o, float x, float y, Hit *h)
{
    float nx = (x - o->cx) / o->rx, ny = (y - o->cy) / o->ry;
    float d = nx * nx + ny * ny;
    if (d > 1.0f) {
        return false;
    }
    h->nx = nx;
    h->ny = ny;
    h->d = d;
    h->seg = 0.0f;
    return true;
}

/* A tapered line from (ax, ay), radius r0, to (bx, by), radius r1. seg is the
 * share of the way along it. */
static bool hit_cone(float x, float y, float ax, float ay, float bx, float by,
                     float r0, float r1, Hit *h)
{
    float vx = bx - ax, vy = by - ay;
    float len2 = vx * vx + vy * vy;
    if (len2 == 0.0f) {
        len2 = 1e-6f; /* as the mockup's "|| 1e-6": no division by zero */
    }
    float t = clampf(((x - ax) * vx + (y - ay) * vy) / len2, 0.0f, 1.0f);
    float dx = x - (ax + vx * t), dy = y - (ay + vy * t);
    float r = r0 + (r1 - r0) * t;
    float dist2 = dx * dx + dy * dy;
    /* Compared squared first: most pixels of the box miss, and this spares
     * them the square root. ! (a <= b) is also true when r is not a number. */
    if (!(dist2 <= r * r) || r <= 0.0f) {
        return false;
    }
    float dist = sqrtf(dist2);
    h->nx = dx / r;
    h->ny = dy / r;
    h->d = (dist / r) * (dist / r);
    h->seg = t;
    return true;
}

/* A tube through the shape's points: the nearest segment wins (v9:143). */
static bool hit_tube(const Shape *s, float x, float y, Hit *best)
{
    bool found = false;
    float last = (float)(s->points - 1);
    for (int i = 0; i < s->points - 1; i++) {
        Hit h;
        float r0 = lerp(s->r0, s->r1, (float)i / last);
        float r1 = lerp(s->r0, s->r1, (float)(i + 1) / last);
        if (hit_cone(x, y, s->pt[i][0], s->pt[i][1], s->pt[i + 1][0], s->pt[i + 1][1],
                     r0, r1, &h) &&
            (!found || h.d < best->d)) {
            *best = h;
            best->seg = (float)i + h.seg;
            found = true;
        }
    }
    return found;
}

/* Inside the triangle of the first three points: the three cross products
 * all have one sign (v9:364). A flat colour, so no slope. */
static bool hit_triangle(const Shape *s, float x, float y, Hit *h)
{
    const float (*q)[2] = s->pt; /* q[i] is point i: a pointer to arrays of 2 */
    float s1 = (q[1][0] - q[0][0]) * (y - q[0][1]) - (q[1][1] - q[0][1]) * (x - q[0][0]);
    float s2 = (q[2][0] - q[1][0]) * (y - q[1][1]) - (q[2][1] - q[1][1]) * (x - q[1][0]);
    float s3 = (q[0][0] - q[2][0]) * (y - q[2][1]) - (q[0][1] - q[2][1]) * (x - q[2][0]);
    bool inside = (s1 >= 0.0f && s2 >= 0.0f && s3 >= 0.0f) ||
                  (s1 <= 0.0f && s2 <= 0.0f && s3 <= 0.0f);
    h->nx = h->ny = h->d = h->seg = 0.0f;
    return inside;
}

static bool hit_shape(const Shape *s, float x, float y, Hit *out)
{
    if (s->triangle) {
        return hit_triangle(s, x, y, out);
    }
    bool found = false;
    for (int i = 0; i < s->ovals; i++) {
        Hit h;
        if (hit_oval(&s->oval[i], x, y, &h) && (!found || h.d < out->d)) {
            *out = h;
            found = true;
        }
    }
    Hit t = {0.0f, 0.0f, 0.0f, 0.0f};
    /* <= : on a tie the tube wins, as the mockup's head does (v9:372). */
    if (s->points >= 2 && hit_tube(s, x, y, &t) && (!found || t.d <= out->d)) {
        *out = t;
        found = true;
    }
    if (found && (s->flags & SHAPE_FLAT)) {
        out->nx *= 0.6f;
        out->ny *= 0.6f;
    }
    return found;
}

/* ---- building the shapes ---- */

static Shape *new_shape(ShapeList *list, int part, int bias)
{
    Shape *s = list->count < SKELETON_SHAPES_MAX ? &list->shape[list->count++] : &list->spare;
    memset(s, 0, sizeof *s);
    s->part = part;
    s->bias = bias;
    s->leg = -1;
    return s;
}

static void add_oval(Shape *s, float cx, float cy, float rx, float ry)
{
    if (s->ovals < SHAPE_OVALS) {
        Oval o = {cx, cy, rx, ry};
        s->oval[s->ovals++] = o;
    }
}

static void add_point(Shape *s, float x, float y)
{
    if (s->points < TUBE_POINTS) {
        s->pt[s->points][0] = x;
        s->pt[s->points][1] = y;
        s->points++;
    }
}

static Shape *new_cone(ShapeList *list, int part, int bias, float ax, float ay,
                       float bx, float by, float r0, float r1)
{
    Shape *s = new_shape(list, part, bias);
    add_point(s, ax, ay);
    add_point(s, bx, by);
    s->r0 = r0;
    s->r1 = r1;
    return s;
}

static Shape *new_ellipse(ShapeList *list, int part, int bias, float cx, float cy,
                          float rx, float ry)
{
    Shape *s = new_shape(list, part, bias);
    add_oval(s, cx, cy, rx, ry);
    return s;
}

/* The box of grid pixels a shape can touch, worked out once it is complete. */
static void shape_box(Shape *s, const Body *b)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < s->ovals; i++) {
        const Oval *o = &s->oval[i];
        x0 = fminf(x0, o->cx - o->rx);
        x1 = fmaxf(x1, o->cx + o->rx);
        y0 = fminf(y0, o->cy - o->ry);
        y1 = fmaxf(y1, o->cy + o->ry);
    }
    float r = s->triangle ? 0.0f : fmaxf(s->r0, s->r1);
    for (int i = 0; i < s->points; i++) {
        x0 = fminf(x0, s->pt[i][0] - r);
        x1 = fmaxf(x1, s->pt[i][0] + r);
        y0 = fminf(y0, s->pt[i][1] - r);
        y1 = fmaxf(y1, s->pt[i][1] + r);
    }
    /* Pixel gx covers x from gx - ox to gx - ox + 1; one pixel of margin. */
    s->gx0 = (int)clampf(floorf(x0) + (float)b->ox - 1.0f, 0.0f, (float)(b->w - 1));
    s->gx1 = (int)clampf(floorf(x1) + (float)b->ox + 1.0f, 0.0f, (float)(b->w - 1));
    s->gy0 = (int)clampf(floorf(y0) + (float)b->oy - 1.0f, 0.0f, (float)(b->h - 1));
    s->gy1 = (int)clampf(floorf(y1) + (float)b->oy + 1.0f, 0.0f, (float)(b->h - 1));
}

/* Two bones of length bone from hip (ax, ay) to foot (fx, fy). The knee sits
 * rise off the straight line between them. A planted foot is nearly as far as
 * the two bones reach, so rise is small (a fraction of a pixel) and the leg
 * reads straight; the closer the foot comes, the more the knee bends. Front
 * knees bend forward, hind hocks backward. Pillar legs never bend. */
static void place_knee(float ax, float ay, float fx, float fy, float bone,
                       bool front, bool pillar, float *kx, float *ky)
{
    float dx = fx - ax, dy = fy - ay;
    float d = sqrtf(dx * dx + dy * dy);
    if (pillar || d < 0.0001f) {  /* the mockup divides by d: guard it */
        *kx = (ax + fx) * 0.5f;
        *ky = (ay + fy) * 0.5f;
        return;
    }
    float half = d * 0.5f;
    float rise = sqrtf(fmaxf(0.0f, bone * bone - half * half));
    float bend = front ? -1.0f : 1.0f;
    *kx = ax + dx * 0.5f - (dy / d) * rise * bend;
    *ky = ay + dy * 0.5f + (dx / d) * rise * bend;
}

/* Where the foot of a leg whose hip is at (ax, ay) goes in this pose
 * (v9:254-267). side 0 is the far side, 1 the near. */
static void place_foot(const Body *b, int side, bool front, float ax, float ay,
                       float *fx, float *fy)
{
    const SpritePose *p = b->p;
    float k = b->k;
    bool run = p->action == ACTION_RUN;
    *fx = ax;
    *fy = 0.0f;
    (void)ay; /* the mockup's feet do not depend on the hip's height */
    if (p->leap != 0.0f) {
        float l = p->leap;
        *fx = ax + (front ? 2.6f * k : -2.4f * k) * l;
        *fy = fminf(0.0f, b->bob * 0.85f) + (front ? -0.8f * k * l : -0.2f * k * l);
    } else if (p->swipe != 0.0f && front && side == 1) {
        *fx = ax + (1.2f + 1.8f * p->swipe) * k;
        *fy = -2.8f * k * p->swipe;
    } else if (p->action == ACTION_WALK || run || p->stepping) {
        float off;
        if (b->sp->hop) {
            off = front ? 0.5f : 0.0f;
        } else {
            off = (front ? PI : 0.0f) + (side ? 0.0f : PI) + (run && front ? 0.6f : 0.0f);
        }
        /* The share of the way through the step, 0..1, also for a negative
         * phase: x - floor(x) is the fraction, as the mockup's "% 1 + 1) % 1". */
        float turns = (p->phase + off) / TWO_PI;
        float u = turns - floorf(turns);
        if (u < 0.6f) {
            *fx = ax + b->stride * (0.5f - u / 0.6f);
        } else {
            float v = (u - 0.6f) / 0.4f;
            *fx = ax + b->stride * (-0.5f + ease_in_out(v));
            *fy = -sinf(PI * v) * b->lift;
        }
        if (b->sp->hop) {
            *fy = fminf(*fy, b->bob * 0.6f);
        }
    } else if (p->fold != 0.0f) {
        *fx = ax + (front ? 1.6f * k : -1.0f * k);
        *fy = -0.3f * k;
    } else if (p->brace != 0.0f && front) {
        *fx += p->brace * 0.8f * k;
    }
}

/* The four legs' hips, knees and feet (v9:250-278). */
static void place_legs(Body *b)
{
    float k = b->k;
    for (int side = 0; side < 2; side++) {
        for (int end = 0; end < 2; end++) {
            bool front = end == 1;
            int leg = side * 2 + end;
            float ax = front ? b->chest_x + 0.3f * k : b->hip_x + 0.3f * k;
            float ay = front ? b->chest_y + b->chest_r * 0.5f : b->by + b->hip_r * 0.45f;
            ax += side ? 0.0f : 0.9f * k; /* the far legs sit a little behind */
            float fx, fy;
            place_foot(b, side, front, ax, ay, &fx, &fy);
            /* The standing leg's length, so a crouch bends the knees; the two
             * bones together just longer than it: straight when planted. */
            float reach = -(ay - b->lower) - 0.3f * k;
            float bone = reach * 0.505f;
            float kx, ky;
            place_knee(ax, ay, fx, fy, bone, front, b->sp->pillar, &kx, &ky);
            b->hip[leg][0] = ax;
            b->hip[leg][1] = ay;
            b->knee[leg][0] = kx;
            b->knee[leg][1] = ky;
            b->foot[leg][0] = fx;
            b->foot[leg][1] = fy;
        }
    }
}

/* The lengths and joints of the body in this pose (v9:239-246, 311-313,
 * 323-324, 358-361). */
static void lay_out(Body *b, const Anatomy *a, const SpritePose *p, bool large)
{
    float k = a->k;
    b->a = a;
    b->sp = &SPECIES[a->species];
    b->p = p;
    b->k = k;
    b->w = large ? SKELETON_DETAIL_W : SKELETON_W;
    b->h = large ? SKELETON_DETAIL_H : SKELETON_H;
    b->ox = large ? SKELETON_DETAIL_OX : SKELETON_OX;
    b->oy = large ? SKELETON_DETAIL_OY : SKELETON_OY;
    b->bob = p->bob * k;
    b->lower = b->bob + p->crouch * k;
    b->stride = p->stride * a->stride;
    b->lift = p->lift * k;
    b->leg_len = a->leg_len * (1.0f - p->fold * 0.85f);
    b->hip_r = a->hip_r + p->breath * k * 0.5f;
    b->chest_r = a->chest_r + p->breath * k;
    b->body_len = a->body_len + p->stretch * k;
    b->bx = p->body_dx * k;
    b->by = -b->leg_len - a->hip_r * 0.95f + b->lower;
    b->hip_x = b->bx - b->body_len * 0.55f;
    b->chest_x = b->bx + b->body_len * 0.45f;
    b->chest_y = b->by - 0.25f * k;
    place_legs(b);

    b->neck_angle = a->neck_angle + p->neck;
    b->nbx = b->chest_x + b->chest_r * 0.35f;
    b->nby = b->chest_y - b->chest_r * 0.35f;
    b->hx = b->nbx + cosf(b->neck_angle) * a->neck_len + a->skull * 0.35f + p->head_dx * k;
    b->hy = b->nby - sinf(b->neck_angle) * a->neck_len - a->skull * 0.3f + p->head_dy * k;
    /* The muzzle's angle follows the neck, the elephant's droops more. */
    float droop = a->species == SPECIES_ELEPHANT ? 0.5f : 0.0f;
    b->md = clampf((b->neck_angle - a->neck_angle) * 0.9f - 0.25f - droop, -1.25f, 0.2f);
    float reach = a->skull * 0.45f + a->muzzle;
    b->mx = b->hx + cosf(b->md) * reach;
    b->my = b->hy - sinf(b->md) * reach + a->skull * 0.3f;

    b->jaw = p->jaw;
    b->jpx = b->hx + a->skull * 0.15f;
    b->jpy = b->hy + a->skull * 0.45f;
    float jaw_len = a->skull * 0.35f + a->muzzle;
    float ja = b->md - b->jaw;
    b->jtx = b->jpx + cosf(ja) * jaw_len;
    b->jty = b->jpy - sinf(ja) * jaw_len;
}

/* One leg's three shapes: thigh, shin and foot (v9:279-286). */
static void add_leg(ShapeList *list, const Body *b, int leg)
{
    bool near = leg >= 2;
    bool pillar = b->sp->pillar;
    int part = near ? PART_LEG : PART_FAR;
    int bias = near ? 0 : -2;
    float k = b->k, w = b->a->leg_w;
    float top = pillar ? w * 0.75f : w * 0.85f;
    float mid = pillar ? w * 0.7f : w * 0.45f;
    float bot = pillar ? w * 0.72f : w * 0.38f;
    float ax = b->hip[leg][0], ay = b->hip[leg][1];
    float kx = b->knee[leg][0], ky = b->knee[leg][1];
    float fx = b->foot[leg][0], fy = b->foot[leg][1];
    /* The thigh starts inside the body, above the hip. */
    new_cone(list, part, bias, ax, ay - 0.8f * k, kx, ky, top, mid)->leg = 0;
    new_cone(list, part, bias, kx, ky, fx, fy - 0.4f * k, mid, bot)->leg = 1;
    Shape *foot;
    if (!pillar) {
        /* Hooves are three shades darker. */
        foot = new_ellipse(list, part, bias - (b->sp->hoof ? 3 : 0), fx + 0.2f * k,
                           fy - 0.4f * k, w * 0.5f, 0.45f * k);
    } else {
        foot = new_ellipse(list, part, bias - 1, fx, fy - 0.35f * k, w * 0.78f, 0.45f * k);
    }
    foot->leg = 2;
}

/* The tail's points: from the root, n steps of len / n, the angle going from
 * a0 to a1 and swung by the pose's tail (v9:290). */
static void tail_curve(Shape *s, const Body *b, float len, float a0, float a1, int n)
{
    float x = b->hip_x - b->a->hip_r * 0.75f;
    float y = b->by - b->a->hip_r * 0.45f;
    add_point(s, x, y);
    for (int i = 1; i <= n; i++) {
        float share = (float)i / (float)n;
        float angle = lerp(a0, a1, share) + b->p->tail * share * 1.6f;
        x -= cosf(angle) * len / (float)n;
        y += sinf(angle) * len / (float)n;
        add_point(s, x, y);
    }
}

/* v9:291-301. */
static void add_tail(ShapeList *list, const Body *b)
{
    float k = b->k;
    Shape *s = new_shape(list, PART_TAIL, 0);
    switch (b->sp->tail) {
    case TAIL_BUSHY:
        tail_curve(s, b, 5.5f * k, 0.4f, 1.2f - b->p->fold * 0.4f, 5);
        s->r0 = 0.9f * k; s->r1 = 1.3f * k;
        break;
    case TAIL_LONG:
        tail_curve(s, b, 7.0f * k, 0.2f, 1.3f, 6);
        s->r0 = 0.6f * k; s->r1 = 0.45f * k;
        break;
    case TAIL_TASSEL:
        tail_curve(s, b, 6.0f * k, 0.5f, 1.3f, 6);
        s->r0 = 0.45f * k; s->r1 = 0.3f * k;
        break;
    case TAIL_RING:
        tail_curve(s, b, 8.0f * k, -0.3f, -2.4f, 8);
        s->r0 = 0.7f * k; s->r1 = 0.55f * k;
        break;
    case TAIL_TUFT:
        tail_curve(s, b, 2.0f * k, -0.9f, -0.4f, 3);
        s->r0 = 0.75f * k; s->r1 = 0.6f * k;
        break;
    case TAIL_PUFF:
        tail_curve(s, b, 0.6f * k, -0.6f, -0.6f, 1);
        s->r0 = 1.0f * k; s->r1 = 1.0f * k;
        break;
    case TAIL_THIN:
        tail_curve(s, b, 3.2f * k, 1.2f, 1.5f, 3);
        s->r0 = 0.3f * k; s->r1 = 0.25f * k;
        break;
    default: /* TAIL_STUB; TAIL_PADDLE until task 7 gives it its own shape */
        tail_curve(s, b, 1.4f * k, 0.3f, 0.6f, 2);
        s->r0 = 0.7f * k; s->r1 = 0.5f * k;
        break;
    }
    if (b->sp->tail == TAIL_TASSEL) {
        const float *end = s->pt[s->points - 1];
        new_ellipse(list, PART_TAIL, 0, end[0], end[1], 0.9f * k, 0.9f * k)->flags = SHAPE_DARK;
    }
}

/* Haunch, chest and belly: three ovals, one shape (v9:303-308). */
static void add_body(ShapeList *list, const Body *b)
{
    Shape *s = new_shape(list, PART_BODY, 0);
    add_oval(s, b->hip_x, b->by, b->hip_r * 1.1f, b->hip_r);
    add_oval(s, b->chest_x, b->chest_y, b->chest_r * 1.05f, b->chest_r);
    add_oval(s, b->bx, b->by + 0.15f * b->k, b->body_len * 0.85f,
             (b->hip_r + b->chest_r) * 0.45f);
}

/* A tapered neck from inside the chest to under the skull (v9:314). */
static void add_neck(ShapeList *list, const Body *b)
{
    float skull = b->a->skull, k = b->k;
    new_cone(list, PART_NECK, 0, b->nbx - 0.5f * k, b->nby + 0.6f * k,
             b->hx - skull * 0.2f, b->hy + skull * 0.15f, b->chest_r * 0.72f, skull * 0.72f);
}

/* One ear: dx shifts it backwards for the far one (v9:325-343). */
static void add_ear(ShapeList *list, const Body *b, float dx, int bias, int part)
{
    float k = b->k, skull = b->a->skull, es = b->a->ear_size;
    float angle = 1.95f + b->p->ear * 0.45f;
    float base_x = b->hx - skull * 0.3f + dx, base_y = b->hy - skull * 0.55f;
    float len, r0, r1;
    int ear = b->sp->ear;
    switch (ear) {
    case EAR_BIG:
        len = 3.4f * k; r0 = 1.25f * k; r1 = 0.3f * k;
        break;
    case EAR_POINTED:
        len = 2.1f * k * es; r0 = 0.9f * k * es; r1 = 0.25f * k;
        break;
    case EAR_LEAF:
        len = 2.3f * k * es; r0 = 0.55f * k; r1 = 0.75f * k * es;
        angle += 0.5f;
        break;
    case EAR_LONG:
        len = 4.2f * k; r0 = 0.65f * k; r1 = 0.75f * k;
        angle = 2.2f + b->p->ear * 0.6f + b->bob * 0.04f;
        break;
    case EAR_FLAP: {
        /* The elephant's ear is a big flat flap on the side of the head, drawn
         * over the neck; it has no far one. */
        if (part == PART_EAR_BACK) {
            return;
        }
        float fl = b->p->flap;
        float fx = b->hx - skull * 0.7f, fy = b->hy + skull * 0.25f;
        new_ellipse(list, PART_EAR, 0, fx - fl * 1.0f * k, fy, skull * (0.95f - fl * 0.25f),
                    skull * 1.25f)->flags = SHAPE_FLAT;
        return;
    }
    default: /* EAR_ROUND */
        len = 0.9f * k; r0 = 0.75f * k; r1 = 0.7f * k;
        break;
    }
    float ex = base_x + cosf(angle) * len, ey = base_y - sinf(angle) * len;
    new_cone(list, part, bias, base_x, base_y, ex, ey, r0, r1);
    if (part == PART_EAR && (ear == EAR_BIG || ear == EAR_POINTED || ear == EAR_LONG)) {
        new_cone(list, PART_EAR, 0, base_x + 0.2f * k, base_y - 0.3f * k, ex + 0.1f * k,
                 ey + 0.5f * k, r0 * 0.42f, r1 * 0.4f)->flags = SHAPE_INNER;
    }
}

/* The open mouth's dark triangle, then the jaw over it (v9:362-367). */
static void add_mouth_and_jaw(ShapeList *list, const Body *b)
{
    const Anatomy *a = b->a;
    if (b->jaw > 0.05f) {
        Shape *s = new_shape(list, PART_MOUTH, 0);
        s->triangle = true;
        add_point(s, b->jpx, b->jpy);
        add_point(s, b->mx, b->my + a->muzzle_r * 0.4f);
        add_point(s, b->jtx, b->jty);
    }
    if (a->species != SPECIES_ELEPHANT) { /* the elephant's trunk hides its jaw */
        new_cone(list, PART_JAW, -1, b->jpx, b->jpy, b->jtx, b->jty, a->skull * 0.42f,
                 a->muzzle_r * 0.62f);
    }
}

/* The skull and the muzzle, one shape (v9:369-373). */
static void add_head(ShapeList *list, const Body *b)
{
    const Anatomy *a = b->a;
    Shape *s = new_shape(list, PART_HEAD, 0);
    add_oval(s, b->hx, b->hy, a->skull * 1.05f, a->skull * 0.95f);
    add_point(s, b->hx + a->skull * 0.2f, b->hy + a->skull * 0.1f);
    add_point(s, b->mx, b->my);
    s->r0 = a->skull * 0.72f;
    s->r1 = a->muzzle_r;
}

/* Every shape in the mockup's order: far legs, tail, body, neck, far ear,
 * mouth, jaw, head, near ear, near legs. Task 7's features go in between. */
static void build_shapes(ShapeList *list, const Body *b)
{
    list->count = 0;
    add_leg(list, b, 0);
    add_leg(list, b, 1);
    add_tail(list, b);
    add_body(list, b);
    add_neck(list, b);
    add_ear(list, b, -0.6f * b->k, -2, PART_EAR_BACK);
    add_mouth_and_jaw(list, b);
    add_head(list, b);
    add_ear(list, b, 0.0f, 0, PART_EAR);
    add_leg(list, b, 2);
    add_leg(list, b, 3);
    Box all = {b->w, b->h, -1, -1};
    for (int i = 0; i < list->count; i++) {
        Shape *s = &list->shape[i];
        shape_box(s, b);
        all.x0 = s->gx0 < all.x0 ? s->gx0 : all.x0;
        all.y0 = s->gy0 < all.y0 ? s->gy0 : all.y0;
        all.x1 = s->gx1 > all.x1 ? s->gx1 : all.x1;
        all.y1 = s->gy1 > all.y1 ? s->gy1 : all.y1;
    }
    all.x0 = all.x0 > 0 ? all.x0 - 1 : 0;
    all.y0 = all.y0 > 0 ? all.y0 - 1 : 0;
    all.x1 = all.x1 < b->w - 1 ? all.x1 + 1 : b->w - 1;
    all.y1 = all.y1 < b->h - 1 ? all.y1 + 1 : b->h - 1;
    list->box = all;
}

/* ---- painting ---- */

/* The shade of a surface with this slope, 0 (deep) to 4 (highlight), moved by
 * bias steps (v9:154). */
static int light_index(const Hit *h, int bias)
{
    float nz = sqrtf(fmaxf(0.0f, 1.0f - h->nx * h->nx - h->ny * h->ny));
    float l = h->nx * LIGHT_X + h->ny * LIGHT_Y + nz * LIGHT_Z;
    int i = l > 0.88f ? 4 : l > 0.62f ? 3 : l > 0.25f ? 2 : l > -0.1f ? 1 : 0;
    i += bias;
    return i < 0 ? 0 : i > 4 ? 4 : i;
}

/* The ink of a pixel the shape s covers (v9:398-408). */
static int ink_of(const Shape *s, const Hit *h, const Body *b)
{
    int part = s->part;
    if (part == PART_MOUTH) {
        return INK_MOUTH;
    }
    int ink;
    if (s->flags & SHAPE_INNER) {
        ink = INK_BELLY;
    } else if (s->flags & SHAPE_DARK) {
        ink = INK_DEEP;
    } else {
        ink = INK_DEEP + light_index(h, s->bias);
    }
    /* The pale belly: the underside of the body and neck. */
    if ((part == PART_BODY || part == PART_NECK) && h->ny > 0.45f && fabsf(h->nx) < 0.8f) {
        ink = INK_BELLY;
    }
    if (part == PART_JAW && h->ny > 0.0f) {
        ink = INK_BELLY;
    }
    if (part == PART_TAIL) {
        int tail = b->sp->tail;
        if (tail == TAIL_TUFT && h->ny > 0.2f) {
            ink = INK_BELLY;
        }
        if (tail == TAIL_PUFF) {
            ink = INK_BELLY; /* the mockup's rule turns every shade of it pale */
        }
        if (tail == TAIL_BUSHY && h->nx < -0.6f && h->ny < 0.2f) {
            ink = INK_BELLY; /* a pale tip */
        }
        if (tail == TAIL_RING && (int)floorf(h->seg * 1.2f) % 2 == 0) {
            ink = INK_STRIPE;
        }
    }
    return ink;
}

/* Each shape over its box, in order: a later shape wins its pixels. */
static void paint(SkeletonRaster *out, const ShapeList *list, const Body *b)
{
    for (int n = 0; n < list->count; n++) {
        const Shape *s = &list->shape[n];
        for (int gy = s->gy0; gy <= s->gy1; gy++) {
            for (int gx = s->gx0; gx <= s->gx1; gx++) {
                /* The pixel's centre, in the body's units. */
                float lx = (float)(gx - b->ox) + 0.5f, ly = (float)(gy - b->oy) + 0.5f;
                Hit h = {0.0f, 0.0f, 0.0f, 0.0f};
                if (!hit_shape(s, lx, ly, &h)) {
                    continue;
                }
                int i = gy * b->w + gx;
                out->part[i] = (uint8_t)s->part;
                out->ink[i] = (uint8_t)ink_of(s, &h, b);
            }
        }
    }
}

static int part_at(const SkeletonRaster *r, int x, int y)
{
    if (x < 0 || y < 0 || x >= r->w || y >= r->h) {
        return PART_NONE;
    }
    return r->part[y * r->w + x];
}

static bool is_shade(int ink)
{
    return ink >= INK_DEEP && ink <= INK_HIGH;
}

/* The four neighbours, right, left, below, above. A table of pairs: DIRS[i][0]
 * is the step in x, DIRS[i][1] in y. */
static const int DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

/* A lone pixel of one shade whose four neighbours, all of its part, share
 * one other shade takes theirs (v9:421-426). Done in place, row by row. */
static void tidy(SkeletonRaster *r, const Box *box)
{
    /* Not the grid's outer ring: those pixels lack a neighbour. */
    int x0 = box->x0 > 1 ? box->x0 : 1, x1 = box->x1 < r->w - 2 ? box->x1 : r->w - 2;
    int y0 = box->y0 > 1 ? box->y0 : 1, y1 = box->y1 < r->h - 2 ? box->y1 : r->h - 2;
    for (int gy = y0; gy <= y1; gy++) {
        for (int gx = x0; gx <= x1; gx++) {
            int i = gy * r->w + gx;
            int v = r->ink[i];
            if (!is_shade(v)) {
                continue;
            }
            int first = -1;
            bool lone = true;
            for (int d = 0; d < 4 && lone; d++) {
                int j = (gy + DIRS[d][1]) * r->w + gx + DIRS[d][0];
                int n = r->ink[j];
                if (r->part[j] != r->part[i] || n == v || (first >= 0 && n != first)) {
                    lone = false;
                }
                first = n;
            }
            if (lone) {
                r->ink[i] = (uint8_t)first;
            }
        }
    }
}

/* A thin dark line where near parts cross the body: near legs over the
 * body, the jaw over the neck, the flap ear and the trunk (v9:428-435). */
static void thin_lines(SkeletonRaster *r, const Body *b, const Box *box)
{
    bool flap = b->sp->ear == EAR_FLAP;
    for (int gy = box->y0; gy <= box->y1; gy++) {
        for (int gx = box->x0; gx <= box->x1; gx++) {
            int i = gy * r->w + gx, pp = r->part[i];
            if (pp != PART_BODY && pp != PART_NECK && pp != PART_FAR && pp != PART_SHELL &&
                pp != PART_MANE && pp != PART_HEAD) {
                continue;
            }
            for (int d = 0; d < 4; d++) {
                int dy = DIRS[d][1];
                int q = part_at(r, gx + DIRS[d][0], gy + dy);
                if ((q == PART_LEG && pp != PART_NECK && pp != PART_HEAD && dy != 1) ||
                    (q == PART_JAW && pp == PART_NECK) || (q == PART_LEG && pp == PART_FAR) ||
                    (q == PART_HEAD && pp == PART_MANE) ||
                    (q == PART_EAR && flap && pp != PART_HEAD) ||
                    (q == PART_TRUNK && pp == PART_HEAD && dy != -1)) {
                    r->ink[i] = INK_OUTLINE;
                    break;
                }
            }
        }
    }
}

/* An empty pixel touching the body becomes outline; where the body under it
 * is lit, the outline is the soft shadow shade instead (v9:462-467). */
static void outline(SkeletonRaster *r, const Box *box)
{
    for (int gy = box->y0; gy <= box->y1; gy++) {
        for (int gx = box->x0; gx <= box->x1; gx++) {
            int i = gy * r->w + gx;
            if (r->part[i] != PART_NONE) {
                continue;
            }
            bool touches = false, lit = false;
            for (int d = 0; d < 4; d++) {
                int x = gx + DIRS[d][0], y = gy + DIRS[d][1];
                if (part_at(r, x, y) == PART_NONE) {
                    continue;
                }
                touches = true;
                int below = r->ink[y * r->w + x];
                if (DIRS[d][1] == 1 &&
                    (below == INK_LIGHT || below == INK_HIGH || below == INK_BELLY)) {
                    lit = true;
                }
            }
            if (touches) {
                r->ink[i] = lit ? INK_SHADOW : INK_OUTLINE;
            }
        }
    }
}

/* A single pixel of ink over whatever is there, as the mockup plots it. The
 * part map is left alone: a nose on the tip of the muzzle may land on the
 * outline, an empty pixel, and stays one. */
static void put_mark(SkeletonRaster *r, int x, int y, int ink)
{
    if (x >= 0 && y >= 0 && x < r->w && y < r->h) {
        r->ink[y * r->w + x] = (uint8_t)ink;
    }
}

/* floorf to an int: the grid pixel a point falls in. */
static int pixel(float v)
{
    return (int)floorf(v);
}

/* Eye, nose and teeth (v9:436-439). The eye is only remembered: blinks are
 * drawn over the picture later. */
static void place_marks(SkeletonRaster *r, const Body *b, bool marks)
{
    const Anatomy *a = b->a;
    float ox = (float)b->ox, oy = (float)b->oy;
    if (!marks) {
        r->eye_x = r->eye_y = -1;
        return;
    }
    r->eye_x = pixel(b->hx + a->skull * 0.38f + ox);
    r->eye_y = pixel(b->hy - a->skull * 0.22f + oy);
    if ((b->sp->features & FEATURE_TRUNK) == 0) {
        put_mark(r, pixel(b->mx + a->muzzle_r * 0.7f + ox),
                 pixel(b->my - a->muzzle_r * 0.25f + oy), INK_NOSE);
    }
    if (a->g.meat > 0.5f && b->jaw > 0.15f) {
        put_mark(r, pixel(b->mx + ox) - 1, pixel(b->my + a->muzzle_r * 0.7f + oy), INK_TEETH);
        put_mark(r, pixel(b->jtx + ox) - 2, pixel(b->jty - a->muzzle_r * 0.5f + oy), INK_TEETH);
    }
}

/* The box of every inked pixel; all 0 when there is none. */
static void measure(SkeletonRaster *r, const Box *box)
{
    r->x0 = r->w;
    r->y0 = r->h;
    r->x1 = r->y1 = 0;
    for (int y = box->y0; y <= box->y1; y++) {
        for (int x = box->x0; x <= box->x1; x++) {
            if (r->ink[y * r->w + x] == INK_NONE) {
                continue;
            }
            r->x0 = x < r->x0 ? x : r->x0;
            r->y0 = y < r->y0 ? y : r->y0;
            r->x1 = x + 1 > r->x1 ? x + 1 : r->x1;
            r->y1 = y + 1 > r->y1 ? y + 1 : r->y1;
        }
    }
    if (r->x1 == 0) {
        r->x0 = r->y0 = 0;
    }
}

void skeleton_pose_standing(SpritePose *p)
{
    memset(p, 0, sizeof *p); /* every number 0, every bool false */
    p->action = ACTION_REST;
    p->eyes = 1.0f;
    p->stride = 1.0f;
    p->lift = 1.1f;
}

void skeleton_raster(SkeletonRaster *out, const Anatomy *a, const SpritePose *p,
                     bool marks, bool large)
{
    Body b;
    ShapeList list; /* about 11 KB on the stack, gone when the function returns */
    lay_out(&b, a, p, large);
    build_shapes(&list, &b);

    memset(out, 0, sizeof *out);
    out->w = b.w;
    out->h = b.h;
    out->ox = b.ox;
    out->oy = b.oy;
    out->shapes = list.count;
    out->eye_big = a->eye_big;
    for (int leg = 0; leg < 4; leg++) {
        for (int c = 0; c < 2; c++) {
            float origin = c == 0 ? (float)b.ox : (float)b.oy;
            out->hip[leg][c] = b.hip[leg][c] + origin;
            out->knee[leg][c] = b.knee[leg][c] + origin;
            out->foot[leg][c] = b.foot[leg][c] + origin;
        }
    }
    out->shadow_rx = a->body_len + 2.5f * a->k;
    out->shadow_ry = 0.9f * a->k + 0.6f;
    out->body_dx = b.bx;

    paint(out, &list, &b);
    tidy(out, &list.box);
    thin_lines(out, &b, &list.box);
    outline(out, &list.box);
    place_marks(out, &b, marks);
    measure(out, &list.box);
}

/* How many traits a standing body's outline depends on (task 6): size, speed,
 * sight, plant, meat and legs. */
#define CAP_TRAITS 6

/* Corner c of the traits the skeleton reads (size, speed, sight, plant, meat,
 * legs): each 0 or 1 by one bit of c. The others stay 0. */
static BodyTraits corner_traits(int c)
{
    BodyTraits t = {0};
    t.size = (float)((c >> 0) & 1);
    t.speed = (float)((c >> 1) & 1);
    t.sight = (float)((c >> 2) & 1);
    t.plant = (float)((c >> 3) & 1);
    t.meat = (float)((c >> 4) & 1);
    t.legs = (float)((c >> 5) & 1);
    return t;
}

/* A body between the corners (a trait at 1/3 or 2/3) can come out one pixel
 * wider than any corner: its head and tail fall on a different part of a
 * pixel. So the corners are held one pixel inside the limits (plan's call),
 * which every one of the 4,096 steps of the six traits then fits. */
#define CAP_ALLOWANCE 1

/* Whether this standing body fits at k: within SPRITE_MAX_W by SPRITE_MAX_H,
 * and clear of the small grid's outer ring, with the allowance to spare. */
static bool fits_at(int species, const BodyTraits *t, float k, SkeletonRaster *r)
{
    SpritePose p;
    Anatomy a;
    skeleton_pose_standing(&p);
    species_anatomy(&a, species, t, k);
    skeleton_raster(r, &a, &p, true, false);
    int edge = 1 + CAP_ALLOWANCE;
    return r->x1 - r->x0 <= SPRITE_MAX_W - CAP_ALLOWANCE &&
           r->y1 - r->y0 <= SPRITE_MAX_H - CAP_ALLOWANCE && r->x0 >= edge &&
           r->y0 >= edge && r->x1 <= r->w - edge && r->y1 <= r->h - edge;
}

/* The cap is the smallest of the corners' own caps. A body only grows with K,
 * so a corner that fits at the cap found so far cannot lower it, and costs one
 * raster; only a corner that does not fit is searched, by halving the gap
 * between a K that fits and one that does not, ten times (to within 0.004). */
float skeleton_k_cap(int species)
{
    /* static: a raster is 22 KB, more than this function should put on the
     * stack. It makes the function unsafe to run on two threads at once, which
     * agon never does. */
    static SkeletonRaster r;
    float cap = 4.0f;
    /* From all traits 1 down to all 0: the widest bodies come early, so the
     * cap falls fast and most later corners fit at once. 1 << n is 2 to the
     * power n. */
    for (int c = (1 << CAP_TRAITS) - 1; c >= 0; c--) {
        BodyTraits t = corner_traits(c);
        if (fits_at(species, &t, cap, &r)) {
            continue;
        }
        float lo = 0.3f, hi = cap;
        for (int step = 0; step < 10; step++) {
            float mid = (lo + hi) * 0.5f;
            if (fits_at(species, &t, mid, &r)) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        cap = lo;
    }
    return cap;
}
