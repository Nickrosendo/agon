/*
 * particles: a fixed pool of small flying pixels, the window's dust, leaves,
 * blood, fur and z's. Drawing only: it has its own random generator and never
 * touches the simulation's. The pool lives inside its owner (no malloc): a
 * full pool drops newcomers instead of growing.
 */
#ifndef AGON_PARTICLES_H
#define AGON_PARTICLES_H

#include <stdbool.h>
#include <stdint.h>

#include "camera.h"
#include "canvas.h"

#define PARTICLES_MAX 1024

/* A one-pixel dot, or the mockup's small 3 by 5 "z" (v9:507). */
enum { PARTICLE_DOT, PARTICLE_Z };

/*
 * One particle, in art pixels of the world, as the mockup's numbers are: a cell
 * is 16 of them whatever the zoom, and drawing scales by ppc / 16. (x, y) is
 * where it started and (vx, vy) its speed then, in art pixels a second; grav
 * pulls it down by 60 * grav art pixels a second, every second (v9:501). Its
 * place at any age comes from particle_position, not from stepping x and y.
 */
typedef struct {
    float x, y, vx, vy;
    float grav, age, life; /* age and life in seconds */
    uint32_t colour;       /* a canvas pixel */
    uint8_t kind;          /* PARTICLE_DOT or PARTICLE_Z */
} Particle;

/* p[0 .. count - 1] are alive. rng is the generator's state; last_ms the body
 * clock at the last step, and started false until the first. */
typedef struct {
    Particle p[PARTICLES_MAX];
    int count;
    uint32_t rng;
    uint64_t last_ms;
    bool started;
} Particles;

/* Empty, with the generator seeded (the scene seeds it with 0x2545F491). */
void particles_init(Particles *ps, uint32_t seed);
/* Empty again, the clock forgotten; the generator goes on where it was. */
void particles_reset(Particles *ps);
/* A copy of *p joins the pool. False when the pool is full: it is dropped. */
bool particles_add(Particles *ps, const Particle *p);
/* 0 <= result < 1, from the pool's own xorshift32. */
float particles_random(Particles *ps);
/* Where p is at this age, in art pixels: its start, plus its speed times the
 * age, plus gravity's 30 * grav * age * age downwards. */
void particle_position(const Particle *p, float age, float *x, float *y);
/* Ages every particle by the body time since the last step, and drops those
 * past their life. The same body_ms twice (paused): nothing changes. */
void particles_step(Particles *ps, uint64_t body_ms);
/* One particle at its age, with its top left art pixel at canvas (x, y), each
 * art pixel scale by scale. The lab draws its own particles with it. */
void particle_paint(const Particle *p, Canvas *c, int x, int y, int scale,
                    CanvasClip clip);
/* Every particle in the camera's view, the view's corner at (view_x, view_y),
 * inside clip. Nothing at the World zoom, where bodies are squares. */
void particles_draw(const Particles *ps, Canvas *c, const Camera *cam, int view_x,
                    int view_y, CanvasClip clip);

#endif
