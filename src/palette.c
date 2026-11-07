#include "palette.h"


/* How many soil colours, from dry to rich. */
/* How many hues of green a plant can be drawn in. */
#define PLANT_HUE_STEPS 16
/* What ripe fruit pulls a plant's colour towards. */
const Colour PALETTE_FRUIT = {235, 130, 40};

#define SOIL_SHADES 6
/* How many looks the ground has, from desert to soaked. */
#define GROUND_SHADES 4

/* Each creature is coloured by the hue in its genome, light enough to
 * stand out from the terrain, and pulled towards a deep red the more of
 * its food is meat. The legend uses the same colours. */
#define CREATURE_SATURATION 0.55f /* 0 white .. 1 full colour */

const Colour PALETTE_WATER = {34, 66, 112};
const Colour PALETTE_SHALLOW = {58, 108, 142};
const Colour PALETTE_RAIN = {176, 200, 232};
const Colour PALETTE_ROCK = {112, 112, 118};
const Colour PALETTE_SOIL_DRY = {132, 110, 78};
const Colour PALETTE_SOIL_RICH = {70, 56, 38};
const Colour PALETTE_SAND = {196, 170, 116};
const Colour PALETTE_SOIL_WET = {40, 38, 36};
const Colour PALETTE_PLANT = {58, 150, 62};
const Colour PALETTE_BLACK = {0, 0, 0};
const Colour PALETTE_WITHERED = {196, 168, 84};
const Colour PALETTE_CARRION = {110, 30, 34};
const Colour PALETTE_SNOW = {236, 240, 246};
const Colour PALETTE_BARE = {104, 78, 56};
const Colour PALETTE_TEXT = {150, 155, 165};
const Colour PALETTE_CURSOR = {255, 255, 255};
const Colour PALETTE_TEXT_BACKGROUND = {14, 17, 23};

/* One colour channel, t of the way from a to b (t in 0..1). */
static uint8_t mix_channel(uint8_t a, uint8_t b, float t)
{
    float v = (float)a + ((float)b - (float)a) * t;
    /* + 0.5f then dropping the fraction rounds to the nearest whole
     * number. v stays inside 0..255, so the conversion is safe; outside
     * that range, converting a float to uint8_t is undefined behaviour. */
    return (uint8_t)(v + 0.5f);
}

Colour palette_mix(Colour a, Colour b, float t)
{
    Colour c = {mix_channel(a.r, b.r, t), mix_channel(a.g, b.g, t),
                mix_channel(a.b, b.b, t)};
    return c;
}

Colour palette_snowed(Colour ground, float snow)
{
    snow = snow < 0.0f ? 0.0f : (snow > 1.0f ? 1.0f : snow);
    return palette_mix(ground, PALETTE_SNOW, snow);
}

Colour palette_soil(float fertility)
{
    /* Fertility in a few steps rather than a smooth blend: neighbouring
     * cells then often share a colour, and term skips repeating a colour,
     * which keeps the frame small. Converting the float to int drops the
     * fraction. */
    int shade = (int)(fertility * (float)SOIL_SHADES);
    if (shade > SOIL_SHADES - 1) {
        shade = SOIL_SHADES - 1; /* fertility exactly 1.0 */
    }
    return palette_mix(PALETTE_SOIL_DRY, PALETTE_SOIL_RICH,
                       (float)shade / (float)(SOIL_SHADES - 1));
}

Colour palette_ground(float fertility, float water)
{
    /* In a few steps, for the same reason palette_soil uses them. */
    static const float SAND[GROUND_SHADES] = {0.75f, 0.35f, 0.0f, 0.0f};
    static const float WET[GROUND_SHADES] = {0.0f, 0.0f, 0.0f, 0.4f};
    int shade = (int)(water * (float)GROUND_SHADES);
    /* An array index from a float: nothing checks it at run time, so the top
     * of the range (water exactly 1) must be pulled back in here. */
    if (shade > GROUND_SHADES - 1) {
        shade = GROUND_SHADES - 1;
    }
    Colour soil = palette_soil(fertility);
    return palette_mix(palette_mix(soil, PALETTE_SAND, SAND[shade]),
                       PALETTE_SOIL_WET, WET[shade]);
}

/* Integer division rounds down, so the + ticks - 1 rounds up instead: the
 * first tick of withering already shows a change of colour. */
int palette_wither_shade(int wither, int ticks)
{
    if (ticks < 1) {
        ticks = 1;
    }
    if (wither > ticks) {
        wither = ticks;
    }
    return (wither * PALETTE_WITHER_SHADES + ticks - 1) / ticks;
}

/*
 * A colour from a hue (0..1 round the colour wheel: 0 red, 1/3 green, 2/3
 * blue, back to red at 1), at full brightness and the given saturation.
 * The wheel has six sectors; in each, one channel is full, one is at its
 * lowest and the third climbs or falls between them.
 */
Colour palette_hue(float hue, float saturation)
{
    float h6 = hue * 6.0f;
    int sector = (int)h6; /* drops the fraction: 0..5 for hue in 0..1 */
    float f = h6 - (float)sector;
    float lo = 1.0f - saturation;
    float rise = lo + saturation * f;
    float fall = 1.0f - saturation * f;
    float r, g, b;
    switch (sector) {
    case 0: r = 1.0f; g = rise; b = lo; break;
    case 1: r = fall; g = 1.0f; b = lo; break;
    case 2: r = lo; g = 1.0f; b = rise; break;
    case 3: r = lo; g = fall; b = 1.0f; break;
    case 4: r = rise; g = lo; b = 1.0f; break;
    default: r = 1.0f; g = lo; b = fall; break; /* 5, or hue exactly 1 */
    }
    /* Each channel is in 0..1, so * 255 + 0.5 stays inside the range a
     * uint8_t can hold and the conversion is safe. */
    Colour c = {(uint8_t)(r * 255.0f + 0.5f), (uint8_t)(g * 255.0f + 0.5f),
                (uint8_t)(b * 255.0f + 0.5f)};
    return c;
}

float render_meat_share(float level)
{
    float t = level - 2.0f;
    if (t < 0.0f) {
        return 0.0f;
    }
    return t > 1.0f ? 1.0f : t;
}

/* A family's hue moved t of the way round the wheel to red (hue 0),
 * whichever way round is shorter: a blue creature (hue 0.67) passes
 * through violet, a yellow one (0.17) through orange. */
static float hue_towards_red(float hue, float t)
{
    float step = hue > 0.5f ? 1.0f - hue : -hue; /* signed distance to 0 */
    float h = hue + step * t;
    return h >= 1.0f ? h - 1.0f : h; /* only 1.0 itself can come out */
}

float render_display_hue(float hue)
{
    float gap = RENDER_PLANT_HUE_TO - RENDER_PLANT_HUE_FROM;
    /* First squeeze 0..1 into 0..1 - gap, then lift everything from the
     * gap's start upwards past the gap. 0 stays 0 (red); a hue just below
     * 1 ends just below 1, so the wheel still closes up at red. */
    float shown = hue * (1.0f - gap);
    return shown >= RENDER_PLANT_HUE_FROM ? shown + gap : shown;
}

float render_plant_hue(float water_need)
{
    float gap = RENDER_PLANT_HUE_TO - RENDER_PLANT_HUE_FROM;
    /* Steps, not a smooth blend: render_world skips cells whose colour has not
     * changed, and with a smooth gene almost every plant would differ. */
    int step = (int)(water_need * (float)PLANT_HUE_STEPS);
    step = step >= PLANT_HUE_STEPS ? PLANT_HUE_STEPS - 1 : step;
    return RENDER_PLANT_HUE_FROM +
           gap * (float)step / (float)(PLANT_HUE_STEPS - 1);
}

/*
 * A plant's full colour: the hue from the water it needs (dry plants yellower,
 * thirsty plants bluer), the saturation from what it offers (a leaf plant a deep
 * green, a fruit tree a paler one), and each whole ripe fruit a step towards
 * orange. Darkened to 60 % so plants sit below the brightness of creatures.
 */
Colour palette_plant(const Plant *p)
{
    int offer = (int)(p->fruit_share * 4.0f); /* 0..4, for 0.84 down to 0.35 */
    float saturation = 0.84f - 0.1225f * (float)offer;
    Colour plant = palette_mix(PALETTE_BLACK,
                               palette_hue(render_plant_hue(p->water_need),
                                           saturation),
                               0.6f);
    /* Comparing before converting: the fruit from a save is only known to be
     * finite, and a large float does not fit an int. */
    int ripe = p->fruit >= 3.0f ? 3 : (int)p->fruit;
    return palette_mix(plant, PALETTE_FRUIT, 0.2f * (float)ripe);
}

Colour palette_creature(float hue, float meat)
{
    hue = render_display_hue(hue); /* hue is a copy: the caller's is safe */
    /* Plant-eaters: pale family colours. Meat-eaters: deep red, with the
     * saturation climbing from 0.55 to 1 as the hue turns. */
    float saturation =
        CREATURE_SATURATION + (1.0f - CREATURE_SATURATION) * meat;
    return palette_hue(hue_towards_red(hue, meat), saturation);
}
