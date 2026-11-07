#include "species.h"

#include <math.h> /* sqrtf */

#include "gfx_hash.h"
#include "sprite.h" /* SPRITE_TWO_TILES, SPRITE_THREE_TILES */

BodyTraits species_traits(const Genome *g)
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

/* A trait cut into SPECIES_TRAIT_STEPS steps, as 0..1: 0, 1/3, 2/3 or 1. */
static float round_trait(float value)
{
    /* Before the cast below: converting a float too large for int is
     * undefined. The test also sends NaN (not a number) to the bottom step,
     * because every comparison with NaN is false. */
    if (value >= 1.0f) {
        return 1.0f;
    }
    if (!(value > 0.0f)) {
        return 0.0f;
    }
    int step = (int)(value * (float)SPECIES_TRAIT_STEPS); /* a cast drops the fraction */
    step = step > SPECIES_TRAIT_STEPS - 1 ? SPECIES_TRAIT_STEPS - 1 : step;
    return (float)step / (float)(SPECIES_TRAIT_STEPS - 1);
}

BodyTraits species_round(const BodyTraits *t)
{
    BodyTraits r = {round_trait(t->size),   round_trait(t->speed),
                    round_trait(t->sight),  round_trait(t->plant),
                    round_trait(t->meat),   round_trait(t->armour),
                    round_trait(t->weapon), round_trait(t->legs),
                    round_trait(t->swim)};
    return r;
}

int species_role(const BodyTraits *t)
{
    if (t->meat > t->plant && t->meat >= PART_FROM) {
        return ROLE_HUNTER;
    }
    if (t->swim >= PART_FROM) {
        return ROLE_SWIMMER;
    }
    if (t->armour >= PART_FROM) {
        return ROLE_ARMOURED;
    }
    if (t->speed >= PART_FROM || t->legs >= PART_FROM) {
        return ROLE_RUNNER;
    }
    if (t->sight >= WATCHER_SIGHT) {
        return ROLE_WATCHER;
    }
    return ROLE_GRAZER;
}

/* The table. Each row is, in order: name, role, colour, then the multipliers
 * scale, body_len, chest, hip, leg_len, leg_w, neck_len, neck_angle (added, so
 * 0 means none), skull, muzzle, muzzle_r, ear_size; then ear, tail; then
 * hoof, pillar, hop, feline; features; stripe colour and frequency. A 1 is the
 * mockup's default. HEX turns 0xrrggbb into a Colour, as the mockup's strings. */
#define HEX(h) {(uint8_t)((h) >> 16), (uint8_t)(((h) >> 8) & 255), (uint8_t)((h) & 255)}
#define NO_STRIPE {0, 0, 0}, 0.0f

const Species SPECIES[SPECIES_COUNT] = {
    [SPECIES_WOLF] = {"wolf", ROLE_HUNTER, HEX(0x8e96a6),
        1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, EAR_POINTED, TAIL_BUSHY,
        false, false, false, false, 0, NO_STRIPE},
    [SPECIES_LION] = {"lion", ROLE_HUNTER, HEX(0xd9a456),
        1, 1.1f, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, EAR_ROUND, TAIL_TASSEL,
        false, false, false, true, FEATURE_MANE, NO_STRIPE},
    [SPECIES_TIGER] = {"tiger", ROLE_HUNTER, HEX(0xe8823a),
        1, 1.2f, 1.05f, 1, 0.85f, 1, 0.7f, 0, 1, 1, 1, 1, EAR_ROUND, TAIL_LONG,
        false, false, false, true, FEATURE_STRIPES | FEATURE_PALE_FACE,
        {30, 20, 24}, 0.6f},
    [SPECIES_OTTER] = {"otter", ROLE_SWIMMER, HEX(0x6b4a32),
        0.8f, 1.35f, 0.8f, 0.85f, 0.45f, 1, 0.5f, -0.35f, 0.85f, 0.7f, 1, 0.6f,
        EAR_ROUND, TAIL_LONG,
        false, false, false, false, FEATURE_PALE_FACE, NO_STRIPE},
    [SPECIES_BEAVER] = {"beaver", ROLE_SWIMMER, HEX(0x7a5232),
        0.9f, 1.1f, 1, 1.25f, 0.45f, 1, 0.3f, -0.3f, 1, 0.8f, 1, 0.6f,
        EAR_ROUND, TAIL_PADDLE,
        false, false, false, false, 0, NO_STRIPE},
    [SPECIES_HIPPO] = {"hippo", ROLE_SWIMMER, HEX(0x9a8288),
        1.45f, 1.2f, 1.15f, 1.15f, 0.6f, 1.9f, 0.15f, -0.35f, 1.4f, 1.3f, 1.6f, 0.5f,
        EAR_ROUND, TAIL_THIN,
        false, true, false, false, FEATURE_FOLDS, NO_STRIPE},
    [SPECIES_ELEPHANT] = {"elephant", ROLE_ARMOURED, HEX(0x8a8f9a),
        1.5f, 1.0f, 1.1f, 1.05f, 1.25f, 1.9f, 0.15f, -0.2f, 1.35f, 0.25f, 1, 1,
        EAR_FLAP, TAIL_THIN,
        false, true, false, false, FEATURE_TRUNK | FEATURE_TUSKS, NO_STRIPE},
    [SPECIES_RHINO] = {"rhino", ROLE_ARMOURED, HEX(0x9a8e86),
        1.25f, 1.15f, 1, 1, 0.9f, 1.6f, 0.2f, -0.45f, 1.15f, 1.3f, 1, 0.7f,
        EAR_POINTED, TAIL_THIN,
        false, true, false, false, FEATURE_RHINO_HORN | FEATURE_FOLDS, NO_STRIPE},
    [SPECIES_ARMADILLO] = {"armadillo", ROLE_ARMOURED, HEX(0xc8b25e),
        0.8f, 1, 1, 1, 0.6f, 1, 0.2f, 0, 1, 1, 1, 1, EAR_ROUND, TAIL_STUB,
        false, false, false, false, FEATURE_SHELL, NO_STRIPE},
    [SPECIES_HARE] = {"hare", ROLE_RUNNER, HEX(0xc8a07a),
        0.85f, 0.8f, 0.95f, 1.25f, 0.55f, 1, 0.3f, 0, 1, 0.6f, 1, 1,
        EAR_LONG, TAIL_PUFF,
        false, false, true, false, 0, NO_STRIPE},
    [SPECIES_GAZELLE] = {"gazelle", ROLE_RUNNER, HEX(0xe8a25a),
        1, 1, 1, 1, 1.05f, 0.8f, 1.1f, 0, 1, 1, 1, 0.8f, EAR_LEAF, TAIL_TUFT,
        true, false, false, false, FEATURE_LYRE_HORNS | FEATURE_SIDE_STRIPE, NO_STRIPE},
    [SPECIES_FOX] = {"fox", ROLE_RUNNER, HEX(0xe07a3a),
        1, 1, 1, 1, 0.75f, 1, 0.6f, 0, 1, 1.25f, 1, 1, EAR_POINTED, TAIL_BUSHY,
        false, false, false, false, FEATURE_STOCKINGS, NO_STRIPE},
    [SPECIES_FENNEC] = {"fennec", ROLE_WATCHER, HEX(0xe6c48e),
        1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, EAR_BIG, TAIL_BUSHY,
        false, false, false, false, 0, NO_STRIPE},
    [SPECIES_LEMUR] = {"lemur", ROLE_WATCHER, HEX(0x9aa0a8),
        1, 1, 1, 1, 1, 1, 1, 0, 1, 0.9f, 1, 1, EAR_ROUND, TAIL_RING,
        false, false, false, false, FEATURE_MASK, NO_STRIPE},
    [SPECIES_DEER] = {"deer", ROLE_GRAZER, HEX(0xb0875a),
        1, 1, 1, 1, 1, 1, 1.2f, 0, 1, 1, 1, 1, EAR_LEAF, TAIL_TUFT,
        true, false, false, false, FEATURE_ANTLERS, NO_STRIPE},
    [SPECIES_ZEBRA] = {"zebra", ROLE_GRAZER, HEX(0xd8d4cc),
        1, 1, 1, 1, 1, 1, 1.1f, 0, 1.05f, 1.5f, 1, 0.8f, EAR_POINTED, TAIL_TASSEL,
        true, false, false, false, FEATURE_STRIPES | FEATURE_NECK_MANE,
        {28, 24, 30}, 0.95f},
    [SPECIES_GOAT] = {"goat", ROLE_GRAZER, HEX(0xa988e8),
        1, 1, 1, 1, 0.85f, 1, 0.8f, 0, 1, 1, 1, 0.7f, EAR_LEAF, TAIL_TUFT,
        true, false, false, false, FEATURE_CURL_HORNS | FEATURE_BEARD, NO_STRIPE},
};

uint32_t species_hash(uint32_t lineage, int role)
{
    /* uint32_t products wrap, which is what a hash wants. */
    return gfx_mix(lineage * 0x9E3779B1u ^ (uint32_t)role * 0x85EBCA77u);
}

/* Whether this species may be drawn at this real size. */
static bool allowed_at(int species, float real_size)
{
    /* !(a >= b) rather than (a < b): a size that is not a number then counts
     * as small, like sprite_tiles_for does. */
    bool small = !(real_size >= SPRITE_TWO_TILES);
    switch (species) {
    case SPECIES_HIPPO:
        return real_size >= SPRITE_THREE_TILES;
    case SPECIES_ARMADILLO:
        return small;
    case SPECIES_ELEPHANT:
    case SPECIES_RHINO:
        return !small;
    default:
        return true;
    }
}

int species_pick(uint32_t lineage, int role, float real_size)
{
    int count = 0;
    for (int s = 0; s < SPECIES_COUNT; s++) {
        if (SPECIES[s].role == role && allowed_at(s, real_size)) {
            count++;
        }
    }
    if (count == 0) {
        return SPECIES_WOLF; /* a role out of range: nothing to choose from */
    }
    int want = (int)(species_hash(lineage, role) % (uint32_t)count);
    for (int s = 0; s < SPECIES_COUNT; s++) {
        if (SPECIES[s].role == role && allowed_at(s, real_size)) {
            if (want == 0) {
                return s;
            }
            want--;
        }
    }
    return SPECIES_WOLF; /* not reached */
}

const float SPECIES_K[SPECIES_K_LEVELS] = {0.70f, 0.80f, 0.95f, 1.05f,
                                           1.20f, 1.35f, 1.50f, 1.65f};

int species_k_level(float real_size)
{
    const float upper[SPECIES_K_LEVELS - 1] = {0.5f, 1.0f, 2.0f, SPRITE_TWO_TILES,
                                               4.5f, SPRITE_THREE_TILES, 9.0f};
    for (int i = 0; i < SPECIES_K_LEVELS - 1; i++) {
        if (!(real_size >= upper[i])) { /* also true for NaN */
            return i;
        }
    }
    return SPECIES_K_LEVELS - 1;
}

void species_anatomy(Anatomy *out, int species, const BodyTraits *rounded, float k)
{
    const Species *sp = &SPECIES[species];
    BodyTraits g = *rounded;
    float root = sqrtf(sp->scale);
    float sz = (0.8f + g.size * 0.5f) * k;
    out->species = species;
    out->k = k;
    out->g = g;
    out->body_len = (4.6f * sz + g.speed * 0.6f * k) * sp->body_len;
    out->chest_r = (2.9f * sz + g.meat * 0.5f * k) * sp->chest;
    out->hip_r = (2.6f * sz + g.plant * 0.2f * k) * sp->hip;
    out->leg_len = (2.4f + g.legs * 2.6f) * k * sp->leg_len;
    out->leg_w = (0.75f + g.size * 0.35f + g.meat * 0.2f) * k * sp->leg_w;
    out->neck_len = (1.4f + g.plant * 1.6f) * k * sp->neck_len;
    out->neck_angle = 0.55f + g.plant * 0.45f - g.meat * 0.35f + sp->neck_angle;
    out->skull = (1.75f + g.size * 0.45f + g.meat * 0.35f) * k * sp->skull / root;
    out->muzzle = (1.3f + g.plant * 0.6f + g.meat * 0.7f - g.sight * 0.4f) * k *
                  sp->muzzle / root;
    out->muzzle_r = (0.75f + g.meat * 0.3f) * k * sp->muzzle_r / root;
    out->ear_size = sp->ear_size * (0.85f + 0.3f * g.sight); /* sight enlarges ears (plan's call) */
    out->eye_big = g.sight > 0.45f || species == SPECIES_LEMUR;
    out->step_hz = (0.9f + g.speed * 0.8f) / root;
    out->stride = (2.0f + g.legs * 2.2f) * k * sp->leg_len;
    out->horn_scale = 0.8f + 0.4f * g.weapon;   /* weapon sizes horns and tusks (plan's call) */
    out->shell_scale = 0.9f + 0.2f * g.armour;  /* armour sizes the shell (plan's call) */
}
