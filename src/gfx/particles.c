#include "particles.h"

#include <math.h> /* floorf */

void particles_init(Particles *ps, uint32_t seed)
{
    ps->count = 0;
    /* xorshift never leaves 0: a zero seed would give 0 for ever. */
    ps->rng = seed != 0 ? seed : 0x2545F491u;
    ps->last_ms = 0;
    ps->started = false;
}

void particles_reset(Particles *ps)
{
    ps->count = 0;
    ps->started = false;
}

bool particles_add(Particles *ps, const Particle *p)
{
    if (ps->count >= PARTICLES_MAX) {
        return false;
    }
    ps->p[ps->count++] = *p; /* a struct assignment copies every field */
    return true;
}

float particles_random(Particles *ps)
{
    /* xorshift32 (Marsaglia): three shifts and xors. On a uint32_t the bits
     * shifted out on the left are simply lost; no overflow is involved. */
    uint32_t x = ps->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    ps->rng = x;
    /* The top 24 bits: a float holds every whole number up to 2^24 exactly,
     * so the result is never rounded up to 1. */
    return (float)(x >> 8) / 16777216.0f;
}

void particle_position(const Particle *p, float age, float *x, float *y)
{
    /* What v9's step (vy += 60 dt grav, then y += vy dt) tends to as dt
     * shrinks, so a particle follows the same path at any frame rate. */
    *x = p->x + p->vx * age;
    *y = p->y + p->vy * age + 30.0f * p->grav * age * age;
}

void particles_step(Particles *ps, uint64_t body_ms)
{
    if (!ps->started || body_ms < ps->last_ms) {
        /* The first step, or a clock that went back (a new world): only
         * start counting from here. Unsigned subtraction would wrap. */
        ps->started = true;
        ps->last_ms = body_ms;
        return;
    }
    float dt = (float)(body_ms - ps->last_ms) / 1000.0f;
    ps->last_ms = body_ms;
    if (dt == 0.0f) {
        return; /* paused */
    }
    int i = 0;
    while (i < ps->count) {
        Particle *p = &ps->p[i];
        p->age += dt;
        if (p->age > p->life) {
            /* The last one takes its place: no gap, and no shifting the rest
             * down. The order changes, which nothing depends on. */
            ps->p[i] = ps->p[--ps->count];
            continue; /* look at the one moved here, at the same i */
        }
        i++;
    }
}

/* The Z's rows, left to right (v9:506). */
static const char *const Z_ROWS[5] = {"###", "..#", ".#.", "#..", "###"};

void particle_paint(const Particle *p, Canvas *c, int x, int y, int scale,
                    CanvasClip clip)
{
    if (p->kind != PARTICLE_Z) {
        canvas_pixel(c, x, y, scale, p->colour, clip);
        return;
    }
    for (int r = 0; r < 5; r++) {
        for (int k = 0; k < 3; k++) {
            if (Z_ROWS[r][k] == '#') {
                canvas_pixel(c, x + k * scale, y + r * scale, scale, p->colour, clip);
            }
        }
    }
}

void particles_draw(const Particles *ps, Canvas *c, const Camera *cam, int view_x,
                    int view_y, CanvasClip clip)
{
    if (camera_sprite_size(cam) == 0) {
        return;
    }
    int ppc = camera_ppc(cam);
    /* An art pixel on screen: 2 at Close, 1 at Normal; at Far, half a pixel,
     * drawn as one. */
    int scale = ppc >= 16 ? ppc / 16 : 1;
    for (int i = 0; i < ps->count; i++) {
        const Particle *p = &ps->p[i];
        float x, y;
        particle_position(p, p->age, &x, &y);
        /* Whole art pixels, as the mockup rounds; floorf, since a plain cast
         * would round a negative number towards zero. */
        float ax = floorf(x + 0.5f), ay = floorf(y + 0.5f);
        int sx, sy;
        camera_to_screen(cam, ax / 16.0f, ay / 16.0f, &sx, &sy);
        particle_paint(p, c, view_x + sx, view_y + sy, scale, clip);
    }
}
