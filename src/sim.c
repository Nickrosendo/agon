#include "sim.h"

#include <stddef.h> /* size_t */

#include "flora.h"
#include "terrain.h"

/* Random starting plants: one draw per soil cell, in row order. */
static void plant_start(World *w, Rng *r, const Settings *s)
{
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->terrain[i] == TERRAIN_SOIL) {
            w->veg[i] = rng_unit(r) < s->veg_start_chance ? 1 : 0;
        }
    }
}

bool sim_init(Sim *s, const Settings *set, uint64_t seed, int w, int h)
{
    s->set = *set; /* copies the whole struct */
    if (!world_init(&s->world, w, h)) {
        return false;
    }
    sim_reseed(s, seed);
    return true;
}

void sim_reseed(Sim *s, uint64_t seed)
{
    s->seed = seed;
    s->tick = 0;
    rng_seed(&s->rng, seed);
    world_clear(&s->world);
    terrain_generate(&s->world, &s->rng, &s->set);
    plant_start(&s->world, &s->rng, &s->set);
}

void sim_step(Sim *s)
{
    flora_step(&s->world, &s->rng, &s->set);
    s->tick++;
}

void sim_free(Sim *s)
{
    world_free(&s->world);
}

/*
 * FNV-1a, a simple hash over bytes: for each byte, mix it in with
 * exclusive-or, then multiply by a fixed prime (wrapping modulo 2^64).
 * data is "const void *", a pointer to bytes of any type; it is read
 * through an unsigned char pointer, the one type C allows to look at the
 * bytes of any object.
 */
static uint64_t fnv1a(uint64_t hash, const void *data, size_t len)
{
    const unsigned char *bytes = data;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= 1099511628211ULL;
    }
    return hash;
}

/*
 * Hashes the state field by field, never the Sim struct's raw bytes:
 * the struct holds pointers (different addresses on every run) and may
 * hold padding, unused bytes the compiler inserts between fields to align
 * them, whose values are unspecified. The arrays hashed here have no
 * padding: uint8_t, uint16_t, float and uint64_t elements sit back to
 * back.
 */
uint64_t sim_checksum(const Sim *s)
{
    const World *w = &s->world;
    size_t n = (size_t)w->w * (size_t)w->h;
    uint64_t hash = 14695981039346656037ULL; /* the FNV starting value */
    hash = fnv1a(hash, &s->tick, sizeof(s->tick));
    hash = fnv1a(hash, s->rng.s, sizeof(s->rng.s));
    hash = fnv1a(hash, w->terrain, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->fertility, n * sizeof(float));
    hash = fnv1a(hash, w->veg, n * sizeof(uint8_t));
    hash = fnv1a(hash, w->veg_age, n * sizeof(uint16_t));
    hash = fnv1a(hash, w->veg_wither, n * sizeof(uint8_t));
    return hash;
}
