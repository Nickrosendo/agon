/*
 * ramp: colours that shift hue as they get lighter or darker, the way pixel
 * artists paint: shadows lean cool, highlights lean warm. Pure maths.
 */
#ifndef AGON_RAMP_H
#define AGON_RAMP_H

#include "palette.h" /* Colour */

enum { RAMP_DEEP, RAMP_SHADOW, RAMP_BASE, RAMP_LIGHT, RAMP_HIGH, RAMP_BELLY,
       RAMP_OUTLINE, RAMP_STEPS };
typedef struct { Colour c[RAMP_STEPS]; } Ramp;
typedef struct { Colour c[3]; } GroundRamp; /* dark, middle, light */

void ramp_to_hsl(Colour c, float *h, float *s, float *l); /* each 0..1 */
Colour ramp_from_hsl(float h, float s, float l);
Ramp ramp_make(float h, float s, float l);
Ramp ramp_from_colour(Colour c); /* ramp_make of its h, s, l */
Colour ramp_shade(Colour c, float k); /* v9:38: k > 0 towards white, k < 0 towards black */
enum { RAMP_FIXED_HORN, RAMP_FIXED_IVORY, RAMP_FIXED_SHELL };
Ramp ramp_fixed(int which); /* from #c9b28a, #efe6d2, #b49a78 */
extern const GroundRamp RAMP_GRASS, RAMP_DIRT, RAMP_ROCK, RAMP_WATER, RAMP_SHALLOW;
extern const Colour RAMP_GLINT, RAMP_FOAM, RAMP_BANK, RAMP_BANK_DARK;
/* Bare soil: dirt turning to grass with fertility, sandy when dry, darker when
 * soaked, whitened by snow (0..1). */
GroundRamp ramp_soil(float fertility, float water, float snow);
/* One step (0 dark, 1 middle, 2 light) of ramp_soil, for when only one is used. */
Colour ramp_soil_step(float fertility, float water, float snow, int step);
/* Every step of g, t of the way towards a colour. */
GroundRamp ramp_ground_mix(GroundRamp g, Colour towards, float t);

#endif
