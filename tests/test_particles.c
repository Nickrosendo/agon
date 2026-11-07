#include "test.h"

#include <math.h>   /* fabsf */
#include <string.h> /* memcmp */

#include "camera.h"
#include "canvas.h"
#include "particles.h"

/* About 40 KB each: static, so they live outside the stack. */
static Particles pool, other;

static Particle dot_at(float x, float y)
{
    return (Particle){.x = x, .y = y, .life = 1.0f, .colour = canvas_rgb(200, 30, 40),
                      .kind = PARTICLE_DOT};
}

static void test_pool_never_overflows(void)
{
    particles_init(&pool, 1u);
    int refused = 0;
    for (int i = 0; i < 2000; i++) {
        Particle p = dot_at((float)i, 0.0f);
        if (!particles_add(&pool, &p)) {
            refused++;
            CHECK(i >= PARTICLES_MAX); /* refused only once the pool is full */
        }
    }
    CHECK(pool.count == PARTICLES_MAX);
    CHECK(refused == 2000 - PARTICLES_MAX);
}

/* Gravity pulls a dot down faster and faster, as the mockup's step does
 * (v9:501: vy += 60 dt grav), and the dot is gone once its life is over. */
static void test_particles_fall_and_die(void)
{
    particles_init(&pool, 1u);
    Particle p = dot_at(10.0f, 10.0f);
    p.vx = 5.0f;
    p.grav = 1.0f;
    p.life = 0.5f;
    CHECK(particles_add(&pool, &p));
    particles_step(&pool, 1000); /* the first step only starts the clock */
    CHECK(pool.p[0].age == 0.0f);
    float x0, y0, x1, y1, x2, y2;
    particle_position(&pool.p[0], 0.0f, &x0, &y0);
    particles_step(&pool, 1100);
    particle_position(&pool.p[0], pool.p[0].age, &x1, &y1);
    particles_step(&pool, 1200);
    particle_position(&pool.p[0], pool.p[0].age, &x2, &y2);
    CHECK(fabsf(pool.p[0].age - 0.2f) < 1e-4f);
    CHECK(x1 > x0 && x2 > x1);                  /* drifting right at vx */
    CHECK(y1 > y0 && y2 - y1 > y1 - y0);        /* falling, faster each step */
    CHECK(fabsf(y2 - (10.0f + 30.0f * 0.04f)) < 1e-3f); /* 60/2 grav t^2 at t 0.2 */
    particles_step(&pool, 1600);
    CHECK(pool.count == 0);
}

/* Paused, the body clock stands still: nothing ages, nothing moves. */
static void test_paused_particles_stay(void)
{
    particles_init(&pool, 1u);
    Particle p = dot_at(10.0f, 10.0f);
    p.vy = -20.0f;
    p.grav = 1.0f;
    CHECK(particles_add(&pool, &p));
    particles_step(&pool, 500);
    particles_step(&pool, 700);
    Particle before = pool.p[0];
    for (int i = 0; i < 10; i++) {
        particles_step(&pool, 700);
    }
    CHECK(pool.count == 1);
    CHECK(pool.p[0].age == before.age);
}

/* The pool's own generator: the same seed gives the same numbers, another seed
 * others, and every number is in 0..1. */
static void test_same_seed_same_particles(void)
{
    particles_init(&pool, 0x2545F491u);
    particles_init(&other, 0x2545F491u);
    bool all_same = true;
    for (int i = 0; i < 100; i++) {
        float a = particles_random(&pool);
        float b = particles_random(&other);
        all_same = all_same && a == b;
        CHECK(a >= 0.0f && a < 1.0f);
    }
    CHECK(all_same);
    particles_init(&other, 7u);
    int differ = 0;
    for (int i = 0; i < 100; i++) {
        differ += particles_random(&pool) != particles_random(&other);
    }
    CHECK(differ > 90);
    /* A seed of 0 would stick at 0 in xorshift: init must cope with it. */
    particles_init(&other, 0u);
    float r = particles_random(&other);
    CHECK(r > 0.0f || particles_random(&other) > 0.0f);
}

/* A dot and a Z drawn half inside a clip: only the inside part shows. */
static void test_draw_clips(void)
{
    Canvas c;
    CHECK(canvas_init(&c, 64, 64));
    uint32_t black = canvas_rgb(0, 0, 0);
    canvas_fill(&c, black);
    Camera cam;
    camera_init(&cam, 8, 8, 64, 64); /* 8 by 8 cells, 16 pixels each at Normal */
    cam.level = CAMERA_NORMAL;
    cam.cx = 2.0f;
    cam.cy = 2.0f; /* world point (2, 2), art pixel (32, 32), at the view's middle */
    particles_init(&pool, 1u);
    Particle z = dot_at(32.0f, 32.0f);
    z.kind = PARTICLE_Z;
    CHECK(particles_add(&pool, &z));
    Particle d = dot_at(20.0f, 20.0f);
    CHECK(particles_add(&pool, &d));
    CanvasClip clip = {0, 0, 33, 64}; /* columns 0..32 only */
    particles_draw(&pool, &c, &cam, 0, 0, clip);
    int inside = 0, outside = 0;
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            if (c.px[y * 64 + x] != black) {
                inside += x < 33;
                outside += x >= 33;
            }
        }
    }
    /* The dot at (20, 20); the Z's first column at 32, its other two cut. */
    CHECK(c.px[20 * 64 + 20] != black);
    CHECK(inside >= 1 + 3);
    CHECK(outside == 0);
    /* At the World zoom bodies are squares: no particles at all. */
    canvas_fill(&c, black);
    cam.level = CAMERA_WORLD;
    particles_draw(&pool, &c, &cam, 0, 0, (CanvasClip){0, 0, 64, 64});
    int any = 0;
    for (int i = 0; i < 64 * 64; i++) {
        any += c.px[i] != black;
    }
    CHECK(any == 0);
    canvas_free(&c);
}

int main(void)
{
    test_pool_never_overflows();
    test_particles_fall_and_die();
    test_paused_particles_stay();
    test_same_seed_same_particles();
    test_draw_clips();
    TEST_SUMMARY();
}
