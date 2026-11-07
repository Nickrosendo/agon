#include "pose.h"

#include <math.h> /* sinf, cosf, powf, fmaxf, fminf */

#define PI 3.14159265f
#define TWO_PI 6.28318531f
/* Art pixels in a cell, whatever the zoom: the mockup's numbers are in them. */
#define ART_PIXELS_PER_CELL 16.0f

/* ---- Easing (v9:160-163) ---- */

float pose_ease_in_out(float u)
{
    if (u < 0.5f) {
        return 2.0f * u * u;
    }
    float v = -2.0f * u + 2.0f;
    return 1.0f - v * v / 2.0f;
}

float pose_ease_out(float u)
{
    float v = 1.0f - u;
    return 1.0f - v * v * v;
}

float pose_ease_back(float u)
{
    const float c1 = 1.9f, c3 = c1 + 1.0f;
    float v = u - 1.0f;
    return 1.0f + c3 * v * v * v + c1 * v * v;
}

float pose_smooth(float a, float b, float x)
{
    float u = (x - a) / (b - a);
    u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
    return u * u * (3.0f - 2.0f * u);
}

static float lerp(float a, float b, float u)
{
    return a + (b - a) * u;
}

/* ---- The cycles. u is the share of the cycle, 0 up to 1. ---- */

/* Walking and running (v9:173-183): the phase goes once round per cycle, and
 * the skeleton places each foot from it. A hopping species bounds on all
 * fours; the others bob twice a step. */
static void pose_walk(SpritePose *p, bool run, bool hop, float u)
{
    float ph = TWO_PI * u;
    p->action = run ? ACTION_RUN : ACTION_WALK;
    p->phase = ph;
    p->stride = run ? 1.6f : 1.0f;
    p->lift = run ? 1.9f : 1.1f;
    if (hop) {
        p->bob = -fmaxf(0.0f, sinf(ph)) * (run ? 3.2f : 2.2f);
        p->stretch = sinf(ph) * 0.6f;
    } else {
        p->bob = -(0.5f + 0.5f * cosf(2.0f * ph)) * (run ? 0.8f : 0.4f);
    }
    p->head_dy = sinf(2.0f * ph - 1.1f) * 0.3f;
    p->tail = sinf(ph - 0.9f) * (run ? 0.12f : 0.22f);
    p->ear = run ? 0.8f : 0.15f + 0.15f * sinf(2.0f * ph - 2.0f);
    p->trunk = sinf(ph - 1.2f) * 0.35f;
    p->flap = 0.2f + 0.2f * sinf(ph);
    if (run && !hop) {
        p->stretch = sinf(2.0f * ph) * 0.5f;
        p->neck = -0.25f;
    }
}

/* Resting (v9:185-196 without the plant-eater's head-down, which is grazing
 * now): two breaths and one swish of the tail a cycle, one flick of the ears
 * in frame flick_frame, and a look up near the end. */
static void pose_rest(SpritePose *p, float u, bool flick)
{
    p->breath = (0.5f + 0.5f * sinf(TWO_PI * 2.0f * u)) * 0.3f;
    p->tail = sinf(TWO_PI * u) * 0.25f;
    p->trunk = sinf(TWO_PI * u) * 0.25f;
    p->flap = fmaxf(0.0f, sinf(TWO_PI * u)) * 0.3f;
    float cyc = u * POSE_REST_SECONDS; /* the mockup counts its 6 s cycle in seconds */
    float look = pose_smooth(5.2f, 5.5f, cyc) * (1.0f - pose_smooth(5.8f, 6.0f, cyc));
    p->neck = 0.2f * look;
    p->ear = -0.3f * look;
    if (flick) {
        p->ear += 0.5f;
    }
}

/* Grazing (v9:188-193): the head goes down over the first quarter, nibbles
 * until three quarters, and comes back up. An elephant lowers its trunk. */
static void pose_graze(SpritePose *p, float u)
{
    float t = u * POSE_GRAZE_SECONDS; /* seconds into the cycle, as the mockup's t */
    float down = pose_smooth(0.0f, 0.25f, u) * (1.0f - pose_smooth(0.75f, 1.0f, u));
    p->action = ACTION_GRAZE;
    p->breath = (0.5f + 0.5f * sinf(TWO_PI * u)) * 0.3f;
    p->tail = sinf(TWO_PI * u) * 0.25f;
    p->neck = -1.2f * down;
    p->head_dx = 0.5f * down;
    p->jaw = down * 0.18f * fmaxf(0.0f, sinf(t * 11.0f));
    p->ear = 0.3f * down;
    p->trunk = -0.6f * down + sinf(t * 3.0f) * 0.15f * down;
    p->flap = fmaxf(0.0f, sinf(TWO_PI * u)) * 0.3f;
}

/* Biting fruit or meat (v9:197-204), in five parts: draw back and brace,
 * lunge with the jaws opening, snap shut, tear with a shake of the head, and
 * settle back while chewing. */
static void pose_bite(SpritePose *p, float c)
{
    p->action = ACTION_BITE;
    if (c < 0.28f) {
        float u = pose_ease_in_out(c / 0.28f);
        p->body_dx = -1.0f * u;
        p->neck = 0.35f * u;
        p->head_dx = -0.6f * u;
        p->jaw = 0.12f * u;
        p->brace = u;
    } else if (c < 0.40f) {
        float u = (c - 0.28f) / 0.12f, e = pose_ease_back(u);
        p->body_dx = lerp(-1.0f, 1.4f, e);
        p->neck = lerp(0.35f, -0.45f, e);
        p->head_dx = lerp(-0.6f, 2.4f, e);
        p->jaw = lerp(0.12f, 0.85f, pose_ease_out(fminf(1.0f, u * 1.3f)));
        p->brace = 1.0f - u;
    } else if (c < 0.47f) {
        float u = pose_ease_out((c - 0.40f) / 0.07f);
        p->body_dx = 1.4f;
        p->neck = -0.45f;
        p->head_dx = 2.4f;
        p->jaw = lerp(0.85f, 0.0f, u);
    } else if (c < 0.66f) {
        float u = (c - 0.47f) / 0.19f;
        float shake = sinf(u * PI * 4.0f) * (1.0f - u);
        p->body_dx = lerp(1.4f, 0.6f, pose_ease_in_out(u));
        p->neck = -0.45f + 0.1f * shake;
        p->head_dx = lerp(2.4f, 1.5f, pose_ease_in_out(u));
        p->head_dy = 0.5f * shake;
    } else {
        float v = (c - 0.66f) / 0.34f, u = pose_ease_in_out(v);
        p->body_dx = lerp(0.6f, 0.0f, u);
        p->neck = lerp(-0.45f, 0.0f, u);
        p->head_dx = lerp(1.5f, 0.0f, u);
        p->jaw = 0.16f * fmaxf(0.0f, sinf(v * PI * 6.0f));
    }
    p->tail = 0.1f + p->body_dx * 0.05f;
    p->ear = 0.5f;
    p->trunk = -0.5f + p->neck;
    p->flap = 0.5f;
}

/* Asleep (v9:231-233): legs folded under, eyes shut, the neck lowered, the
 * head drawn back, the ears laid down, one slow breath per cycle. */
static void pose_sleep(SpritePose *p, float u)
{
    p->action = ACTION_SLEEP;
    p->fold = 1.0f;
    p->eyes = 0.0f;
    p->neck = -0.9f;
    p->head_dx = -0.5f;
    p->trunk = -0.3f;
    p->tail = 0.5f;
    p->ear = 0.8f;
    /* The mockup breathes with sin(t * 1.1), which does not fit a whole number
     * of times into a 3 second loop and would jump where the loop starts
     * again. One breath per cycle keeps its depth and loops cleanly. */
    p->breath = (0.5f + 0.5f * sinf(TWO_PI * u)) * 0.45f;
}

void pose_at(SpritePose *out, const SpriteBody *b, int action, int frame)
{
    skeleton_pose_standing(out);
    if (action < 0 || action >= ACTION_COUNT) {
        return;
    }
    int n = SPRITE_FRAMES[action];
    frame = (frame % n + n) % n; /* % keeps a negative sign: see sprite.c */
    float u = (float)frame / (float)n;
    bool hop = SPECIES[b->species].hop;
    switch (action) {
    case ACTION_WALK:
    case ACTION_RUN:
        pose_walk(out, action == ACTION_RUN, hop, u);
        break;
    case ACTION_REST:
        /* The ear flick takes 0.15 s, under one 0.25 s frame: it is the frame
         * that holds the share 0.30, so some frame always shows it. */
        pose_rest(out, u, frame == (int)(0.30f * (float)n));
        break;
    case ACTION_GRAZE:
        pose_graze(out, u);
        break;
    case ACTION_BITE:
        pose_bite(out, u);
        break;
    case ACTION_SLEEP:
        pose_sleep(out, u);
        break;
    default:
        break; /* the attack stands until its own task */
    }
}

/* The body's own lengths: its anatomy at its K. */
static void anatomy_of(Anatomy *a, const SpriteBody *b)
{
    species_anatomy(a, b->species, &b->traits, sprite_k(b));
}

float pose_cycle_seconds(const SpriteBody *b, int action)
{
    switch (action) {
    case ACTION_WALK:
    case ACTION_RUN: {
        /* A block in braces: a case cannot start with a declaration in C11. */
        Anatomy a;
        anatomy_of(&a, b);
        float hz = a.step_hz * (action == ACTION_RUN ? POSE_RUN_RATE : 1.0f);
        return hz > 0.0f ? 1.0f / hz : 0.0f;
    }
    case ACTION_REST:
        return POSE_REST_SECONDS;
    case ACTION_GRAZE:
        return POSE_GRAZE_SECONDS;
    case ACTION_BITE:
        return POSE_BITE_SECONDS;
    case ACTION_SLEEP:
        return POSE_SLEEP_SECONDS;
    default:
        return 0.0f;
    }
}

float pose_cadence(const SpriteBody *b, int action, float cells_per_second)
{
    if (action != ACTION_WALK && action != ACTION_RUN) {
        return 1.0f;
    }
    /* The stride at Normal, where a cell is 16 art pixels: the same answer at
     * every zoom, so a creature's frame does not change when the view does. */
    SpriteBody normal = *b; /* a struct copy: b itself is not changed */
    normal.base = SPRITE_BASE_NORMAL;
    Anatomy a;
    anatomy_of(&a, &normal);
    float stride = a.stride * (action == ACTION_RUN ? 1.6f : 1.0f);
    float cycle = pose_cycle_seconds(&normal, action);
    float cadence = cells_per_second * ART_PIXELS_PER_CELL * cycle / (2.0f * stride);
    /* !(x >= 1) is also true for NaN, which every comparison fails. */
    if (!(cadence >= 1.0f)) {
        return 1.0f;
    }
    return cadence > POSE_CADENCE_MAX ? POSE_CADENCE_MAX : cadence;
}

/* A share of 2^32 from the id: id times 2654435769 (2^32 divided by the golden
 * ratio), wrapping at 2^32, spreads ids evenly with neighbours far apart. The
 * uint32_t multiply wraps on purpose: unsigned overflow is defined in C,
 * signed overflow is not. */
static uint32_t id_share(uint32_t id)
{
    return id * 2654435769u;
}

int pose_frame(uint64_t ms, uint32_t id, int action, float cycle_seconds)
{
    if (action < 0 || action >= ACTION_COUNT || !(cycle_seconds > 0.0f)) {
        return 0;
    }
    uint64_t n = (uint64_t)SPRITE_FRAMES[action];
    uint64_t cycle_ms = (uint64_t)(cycle_seconds * 1000.0f + 0.5f);
    if (cycle_ms == 0) {
        cycle_ms = 1; /* a cycle under half a millisecond: never divide by 0 */
    }
    /* How far through the cycle the clock is, as a frame number. */
    uint64_t by_time = (ms % cycle_ms) * n / cycle_ms;
    /* The id's share of the cycle as a frame: share * n, keeping the top 32
     * bits of the 64-bit product. */
    uint64_t by_id = ((uint64_t)id_share(id) * n) >> 32;
    return (int)((by_time + by_id) % n);
}

#define BLINK_EVERY_MS 3600u
#define BLINK_STEP_MS 60u

float pose_eyes(uint64_t ms, uint32_t id)
{
    uint64_t shift = ((uint64_t)id_share(id) * BLINK_EVERY_MS) >> 32;
    uint64_t at = (ms + shift) % BLINK_EVERY_MS;
    if (at < BLINK_STEP_MS) {
        return 0.4f;
    }
    if (at < 2u * BLINK_STEP_MS) {
        return 0.0f;
    }
    if (at < 3u * BLINK_STEP_MS) {
        return 0.4f;
    }
    return 1.0f;
}
