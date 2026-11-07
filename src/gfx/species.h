/*
 * species: which animal a family is drawn as, and the numbers of its body.
 * Everything here is drawing-side: a family's species is worked out from its
 * founder's id and its role each time it is needed, never stored or saved, and
 * never with the simulation's random numbers. A port of the mockup's SPECIES,
 * ROLES and anatomy() (attack-v9.html, lines 89-128).
 */
#ifndef AGON_SPECIES_H
#define AGON_SPECIES_H

#include <stdbool.h>
#include <stdint.h>

#include "genome.h"
#include "palette.h" /* Colour */

/* The inherited traits a body shows, each 0..1. The last four are parts:
 * armour shows as plates, weapon as a tusk and then a horn, legs as long legs
 * and swim as a fin tail and a fin on the back. */
typedef struct {
    float size, speed, sight, plant, meat;
    float armour, weapon, legs, swim;
} BodyTraits;

BodyTraits species_traits(const Genome *g);

/* A part gene from here up shows its part, and decides some roles. */
#define PART_FROM 0.5f
/* "Sight high" (plan's call). On rounded traits (0, 1/3, 2/3, 1) this is the
 * top step only: a raw sight gene of 0.75 or more. */
#define WATCHER_SIGHT 0.7f
#define SPECIES_TRAIT_STEPS 4

/* Every trait cut to 0, 1/3, 2/3 or 1, so a small mutation changes nothing. */
BodyTraits species_round(const BodyTraits *t);

enum { ROLE_HUNTER, ROLE_SWIMMER, ROLE_ARMOURED, ROLE_RUNNER, ROLE_WATCHER,
       ROLE_GRAZER, ROLE_COUNT };
/* The role of rounded traits: the first rule that matches wins. */
int species_role(const BodyTraits *rounded);

enum { SPECIES_WOLF, SPECIES_LION, SPECIES_TIGER, SPECIES_OTTER, SPECIES_BEAVER,
       SPECIES_HIPPO, SPECIES_ELEPHANT, SPECIES_RHINO, SPECIES_ARMADILLO,
       SPECIES_HARE, SPECIES_GAZELLE, SPECIES_FOX, SPECIES_FENNEC, SPECIES_LEMUR,
       SPECIES_DEER, SPECIES_ZEBRA, SPECIES_GOAT, SPECIES_COUNT };
enum { EAR_POINTED, EAR_LEAF, EAR_ROUND, EAR_BIG, EAR_LONG, EAR_FLAP };
enum { TAIL_BUSHY, TAIL_TUFT, TAIL_PUFF, TAIL_TASSEL, TAIL_RING, TAIL_LONG,
       TAIL_THIN, TAIL_STUB, TAIL_PADDLE };
/* Features are bits of one number: a species has several at once. */
enum { FEATURE_ANTLERS = 1u << 0, FEATURE_CURL_HORNS = 1u << 1,
       FEATURE_LYRE_HORNS = 1u << 2, FEATURE_RHINO_HORN = 1u << 3,
       FEATURE_TUSKS = 1u << 4, FEATURE_TRUNK = 1u << 5, FEATURE_MANE = 1u << 6,
       FEATURE_NECK_MANE = 1u << 7, FEATURE_BEARD = 1u << 8, FEATURE_SHELL = 1u << 9,
       FEATURE_STRIPES = 1u << 10, FEATURE_SIDE_STRIPE = 1u << 11,
       FEATURE_STOCKINGS = 1u << 12, FEATURE_MASK = 1u << 13,
       FEATURE_PALE_FACE = 1u << 14, FEATURE_FOLDS = 1u << 15 };

typedef struct {
    const char *name;
    int role;
    Colour look;      /* the mockup's colour: its saturation and lightness are used, the hue is the family's */
    float scale, body_len, chest, hip, leg_len, leg_w, neck_len, neck_angle,
          skull, muzzle, muzzle_r, ear_size;  /* multipliers, 1 by default; neck_angle is added, 0 by default */
    int ear, tail;
    bool hoof, pillar, hop, feline;
    unsigned features;
    Colour stripe; float stripe_freq;
} Species;
extern const Species SPECIES[SPECIES_COUNT];

/* A number from the founder's id and the role, the same every time. */
uint32_t species_hash(uint32_t lineage, int role);
/* The family's species in this role at this real size: species_hash modulo the
 * species allowed at that size, in table order. */
int species_pick(uint32_t lineage, int role, float real_size);

#define SPECIES_K_LEVELS 8
/* The scale a body is drawn at, by size level. */
extern const float SPECIES_K[SPECIES_K_LEVELS];
/* The level of a real size: below 0.5, 1, 2, SPRITE_TWO_TILES, 4.5,
 * SPRITE_THREE_TILES, 9 give levels 0 to 6; anything larger gives 7. A size
 * that is not a number gives 0. */
int species_k_level(float real_size);

typedef struct {
    int species; float k;
    float body_len, chest_r, hip_r, leg_len, leg_w, neck_len, neck_angle, skull,
          muzzle, muzzle_r, ear_size, step_hz, stride, horn_scale, shell_scale;
    bool eye_big;
    BodyTraits g;   /* the rounded genes */
} Anatomy;
/* The mockup's anatomy(): the lengths of one body, from its species, its
 * rounded genes and the scale k (which already includes the species' scale). */
void species_anatomy(Anatomy *out, int species, const BodyTraits *rounded, float k);

#endif
