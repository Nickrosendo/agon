#include "ramp.h"

#include <math.h> /* floorf */

static float clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/* v minus its floor: the part after the point, always 0 <= result < 1, so a
 * hue that went past 1 or below 0 comes back round the colour wheel. */
static float wrap01(float v)
{
    return v - floorf(v);
}

/* A colour from three channels given in 0..255 as floats, to the nearest byte. */
static Colour colour_from_floats(float r, float g, float b)
{
    /* Clamped first: converting an out of range float to uint8_t is undefined. */
    Colour c = {(uint8_t)(clamp(r, 0.0f, 255.0f) + 0.5f),
                (uint8_t)(clamp(g, 0.0f, 255.0f) + 0.5f),
                (uint8_t)(clamp(b, 0.0f, 255.0f) + 0.5f)};
    return c;
}

/* The mockup's rgb2hsl (v9:40). */
void ramp_to_hsl(Colour c, float *h, float *s, float *l)
{
    float r = (float)c.r / 255.0f, g = (float)c.g / 255.0f,
          b = (float)c.b / 255.0f;
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    *h = 0.0f;
    *s = 0.0f;
    *l = (mx + mn) / 2.0f;
    if (mx != mn) {
        float d = mx - mn;
        *s = *l > 0.5f ? d / (2.0f - mx - mn) : d / (mx + mn);
        if (mx == r) {
            *h = (g - b) / d + (g < b ? 6.0f : 0.0f);
        } else if (mx == g) {
            *h = (b - r) / d + 2.0f;
        } else {
            *h = (r - g) / d + 4.0f;
        }
        *h /= 6.0f;
    }
}

/* One channel of hsl2rgb (v9:48): p and q are the low and high ends, t the
 * position round the wheel. */
static float hue_channel(float p, float q, float t)
{
    t = wrap01(t);
    if (t < 1.0f / 6.0f) {
        return p + (q - p) * 6.0f * t;
    }
    if (t < 0.5f) {
        return q;
    }
    if (t < 2.0f / 3.0f) {
        return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
    }
    return p;
}

Colour ramp_from_hsl(float h, float s, float l)
{
    if (s == 0.0f) {
        return colour_from_floats(l * 255.0f, l * 255.0f, l * 255.0f);
    }
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    return colour_from_floats(hue_channel(p, q, h + 1.0f / 3.0f) * 255.0f,
                              hue_channel(p, q, h) * 255.0f,
                              hue_channel(p, q, h - 1.0f / 3.0f) * 255.0f);
}

Ramp ramp_make(float h, float s, float l)
{
    /* hue shift in degrees, saturation change, lightness change */
    static const float STEP[6][3] = {
        {-16.0f, 0.04f, -0.28f}, {-8.0f, 0.02f, -0.14f}, {0.0f, 0.0f, 0.0f},
        {6.0f, -0.04f, 0.10f},   {12.0f, -0.12f, 0.21f}, {14.0f, -0.25f, 0.17f}};
    Ramp r;
    for (int i = 0; i < 6; i++) {
        r.c[i] = ramp_from_hsl(wrap01(h + STEP[i][0] / 360.0f),
                               clamp(s + STEP[i][1], 0.0f, 1.0f),
                               clamp(l + STEP[i][2], 0.02f, 0.97f));
    }
    /* The outline: a very dark version of the body, not black. */
    r.c[RAMP_OUTLINE] = ramp_from_hsl(wrap01(h - 35.0f / 360.0f), s,
                                      clamp(l * 0.2f, 0.02f, 0.97f));
    return r;
}

Ramp ramp_from_colour(Colour c)
{
    float h, s, l;
    ramp_to_hsl(c, &h, &s, &l); /* & passes where to write each answer */
    return ramp_make(h, s, l);
}

Colour ramp_shade(Colour c, float k)
{
    static const Colour WHITE = {255, 255, 255};
    return k >= 0.0f ? palette_mix(c, WHITE, k) : palette_mix(c, PALETTE_BLACK, -k);
}

Ramp ramp_fixed(int which)
{
    static const Colour BASE[3] = {{0xc9, 0xb2, 0x8a},
                                   {0xef, 0xe6, 0xd2},
                                   {0xb4, 0x9a, 0x78}};
    return ramp_from_colour(BASE[which]);
}

const GroundRamp RAMP_GRASS = {{{0x2c, 0x4a, 0x2a}, {0x36, 0x5a, 0x31}, {0x41, 0x69, 0x3a}}};
const GroundRamp RAMP_DIRT = {{{0x5a, 0x3d, 0x26}, {0x6b, 0x4a, 0x2d}, {0x7c, 0x57, 0x35}}};
const GroundRamp RAMP_ROCK = {{{0x5c, 0x5a, 0x63}, {0x6e, 0x6c, 0x76}, {0x84, 0x81, 0x8c}}};
const GroundRamp RAMP_WATER = {{{0x17, 0x3a, 0x5c}, {0x1d, 0x48, 0x70}, {0x24, 0x55, 0x84}}};
const GroundRamp RAMP_SHALLOW = {{{0x22, 0x55, 0x7a}, {0x2a, 0x68, 0x90}, {0x34, 0x7a, 0xa2}}};
const Colour RAMP_GLINT = {0x7f, 0xc0, 0xe8};
const Colour RAMP_FOAM = {0xd8, 0xf0, 0xff};
const Colour RAMP_BANK = {0x4a, 0x30, 0x20};
const Colour RAMP_BANK_DARK = {0x36, 0x22, 0x0f};

GroundRamp ramp_ground_mix(GroundRamp g, Colour towards, float t)
{
    for (int i = 0; i < 3; i++) {
        g.c[i] = palette_mix(g.c[i], towards, t); /* g is a copy: safe to change */
    }
    return g;
}

GroundRamp ramp_soil(float fertility, float water, float snow)
{
    static const float SAND_SHADE[3] = {-0.15f, 0.0f, 0.1f};
    GroundRamp g;
    for (int i = 0; i < 3; i++) {
        Colour c = palette_mix(RAMP_DIRT.c[i], RAMP_GRASS.c[i], fertility);
        if (water < 0.25f) {
            float dry = (0.25f - water) / 0.25f * 0.5f;
            c = palette_mix(c, ramp_shade(PALETTE_SAND, SAND_SHADE[i]), dry);
        } else if (water > 0.75f) {
            c = ramp_shade(c, -0.15f * (water - 0.75f) / 0.25f);
        }
        g.c[i] = palette_snowed(c, snow);
    }
    return g;
}
