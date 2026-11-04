/*
 * palette: the colours of the world and the sums behind them. Pure maths,
 * shared by the terminal program and the window program, so a plant or a
 * creature has the same colour in both.
 */
#ifndef AGON_PALETTE_H
#define AGON_PALETTE_H

#include <stdint.h>

#include "world.h" /* Plant */

typedef struct {
    uint8_t r, g, b;
} Colour;

/*
 * extern says "this exists, in some other .c file": the one real copy is
 * in palette.c, and the linker connects every use to it. A header cannot
 * simply define a variable: each .c file including it would get its own
 * definition, and the link would fail.
 */
extern const Colour PALETTE_WATER;
extern const Colour PALETTE_SHALLOW;
extern const Colour PALETTE_RAIN;
extern const Colour PALETTE_SAND;
extern const Colour PALETTE_SOIL_WET;
extern const Colour PALETTE_ROCK;
extern const Colour PALETTE_SOIL_DRY;
extern const Colour PALETTE_SOIL_RICH;
extern const Colour PALETTE_PLANT; /* the legend's plant colour */
extern const Colour PALETTE_BLACK;
extern const Colour PALETTE_WITHERED; /* dry straw */
extern const Colour PALETTE_CARRION;  /* dried blood */
extern const Colour PALETTE_TEXT;
extern const Colour PALETTE_CURSOR;
extern const Colour PALETTE_TEXT_BACKGROUND;

/* How many colour steps a withering plant goes through on its way from
 * green to straw. */
#define PALETTE_WITHER_SHADES 4

/* A colour t of the way from a to b (t in 0..1). */
Colour palette_mix(Colour a, Colour b, float t);

/* A colour from a hue (0..1 round the colour wheel: 0 red, 1/3 green, 2/3
 * blue, back to red at 1), at full brightness and the given saturation. */
Colour palette_hue(float hue, float saturation);

/* A plant's full colour, from its genes and its ripe fruit. */
Colour palette_plant(const Plant *p);

/* A creature's colour, from its inherited hue (0..1) and meat share (0..1). */
Colour palette_creature(float hue, float meat);

/* Bare soil's colour, from its fertility (0..1), in a few steps. */
Colour palette_soil(float fertility);
/* The same soil as its water changes it: sand when dry, darker when soaked. */
Colour palette_ground(float fertility, float water);

/* How far a withering plant has gone, from 1 (just started) to
 * PALETTE_WITHER_SHADES (about to disappear). */
int palette_wither_shade(int wither, int ticks);

/* How much of a creature's food is meat, from its food-chain level: 0 at
 * level 2 or below (plants only), 1 at level 3 or above (meat only). */
float render_meat_share(float level);

/* The greens plants are drawn in, as hues (0..1 round the colour wheel:
 * 0 red, 1/3 green). Creatures are never drawn in them. */
#define RENDER_PLANT_HUE_FROM 0.20f
#define RENDER_PLANT_HUE_TO 0.47f

/* The hue a plant is drawn in, from how much water it needs: one of 16 hues
 * spread across the plants' greens, so patches adapted to different ground look
 * different. */
float render_plant_hue(float water_need);

/* The hue a creature is drawn in, from its inherited hue: the whole wheel
 * squeezed into the part outside the plants' greens, order kept, so a
 * creature never looks like a plant and close families stay close. */
float render_display_hue(float hue);

#endif
