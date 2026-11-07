#include "test.h"

#include <math.h>   /* fabsf */
#include <stdlib.h> /* abs */

#include "gfx_hash.h"
#include "palette.h"
#include "ramp.h"

static int same(Colour a, Colour b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b;
}

/* How far apart two hues (0..1 round the wheel) are, in degrees, the short way. */
static float hue_gap_degrees(float a, float b)
{
    float d = fabsf(a - b);
    d = d > 0.5f ? 1.0f - d : d;
    return d * 360.0f;
}

static void test_hsl_round_trip(void)
{
    int worst = 0;
    for (int r = 0; r < 256; r += 8) {
        for (int g = 0; g < 256; g += 8) {
            for (int b = 0; b < 256; b += 8) {
                Colour c = {(uint8_t)r, (uint8_t)g, (uint8_t)b};
                float h, s, l;
                ramp_to_hsl(c, &h, &s, &l);
                Colour back = ramp_from_hsl(h, s, l);
                int d = abs((int)back.r - r);
                d = abs((int)back.g - g) > d ? abs((int)back.g - g) : d;
                d = abs((int)back.b - b) > d ? abs((int)back.b - b) : d;
                worst = d > worst ? d : worst;
            }
        }
    }
    CHECK(worst <= 1);
}

static void test_ramp_shifts_hue(void)
{
    Colour base = {0xb0, 0x87, 0x5a};
    float h, s, l;
    ramp_to_hsl(base, &h, &s, &l);
    Ramp r = ramp_from_colour(base);
    float hd, sd, ld, hh, sh, lh;
    ramp_to_hsl(r.c[RAMP_DEEP], &hd, &sd, &ld);
    ramp_to_hsl(r.c[RAMP_HIGH], &hh, &sh, &lh);
    CHECK(hue_gap_degrees(hd, h - 16.0f / 360.0f + 1.0f) < 1.0f);
    /* The highlight is pale (saturation 0.23), so rounding its colour to whole
     * bytes moves its hue by up to about 1.2 degrees; 1.5 allows for that. */
    CHECK(hue_gap_degrees(hh, h + 12.0f / 360.0f) < 1.5f);
    CHECK(ld < l && l < lh);
    float prev = -1.0f;
    for (int i = RAMP_DEEP; i <= RAMP_HIGH; i++) {
        float hi, si, li;
        ramp_to_hsl(r.c[i], &hi, &si, &li);
        CHECK(li > prev);
        prev = li;
    }
}

static void test_outline_is_dark_in_its_own_hue(void)
{
    Colour base = {0xb0, 0x87, 0x5a};
    float h, s, l;
    ramp_to_hsl(base, &h, &s, &l);
    Ramp r = ramp_from_colour(base);
    float ho, so, lo;
    ramp_to_hsl(r.c[RAMP_OUTLINE], &ho, &so, &lo);
    CHECK(fabsf(lo - 0.2f * l) < 0.01f);
    CHECK(hue_gap_degrees(ho, h - 35.0f / 360.0f + 1.0f) < 2.0f);
    CHECK(!same(r.c[RAMP_OUTLINE], PALETTE_BLACK));
    /* Even a black body gets an outline that is not pure black. */
    Ramp dark = ramp_make(0.0f, 0.0f, 0.0f);
    CHECK(!same(dark.c[RAMP_OUTLINE], PALETTE_BLACK));
}

static void test_snowed(void)
{
    Colour ground = {40, 90, 50};
    CHECK(same(palette_snowed(ground, 0.0f), ground));
    CHECK(same(palette_snowed(ground, 1.0f), PALETTE_SNOW));
    CHECK(same(palette_snowed(ground, 3.0f), PALETTE_SNOW));
    CHECK(same(palette_snowed(ground, -2.0f), ground));
}

static void test_soil_ramp(void)
{
    GroundRamp dirt = ramp_soil(0.0f, 0.5f, 0.0f);
    GroundRamp grass = ramp_soil(1.0f, 0.5f, 0.0f);
    GroundRamp snow = ramp_soil(0.3f, 0.5f, 1.0f);
    GroundRamp half = ramp_soil(0.0f, 0.5f, 0.5f);
    for (int i = 0; i < 3; i++) {
        CHECK(same(dirt.c[i], RAMP_DIRT.c[i]));
        CHECK(same(grass.c[i], RAMP_GRASS.c[i]));
        CHECK(same(snow.c[i], PALETTE_SNOW));
        CHECK(same(half.c[i], palette_mix(RAMP_DIRT.c[i], PALETTE_SNOW, 0.5f)));
    }
    GroundRamp dry = ramp_soil(0.5f, 0.0f, 0.0f);
    GroundRamp mid = ramp_soil(0.5f, 0.5f, 0.0f);
    GroundRamp soaked = ramp_soil(0.5f, 1.0f, 0.0f);
    for (int i = 0; i < 3; i++) {
        int dry_sum = dry.c[i].r + dry.c[i].g + dry.c[i].b;
        int mid_sum = mid.c[i].r + mid.c[i].g + mid.c[i].b;
        int soaked_sum = soaked.c[i].r + soaked.c[i].g + soaked.c[i].b;
        CHECK(dry_sum > mid_sum);
        CHECK(soaked_sum < mid_sum);
    }
}

static void test_hash_in_range_and_stable(void)
{
    double total = 0.0;
    int in_range = 1, stable = 1;
    for (int i = 0; i < 100000; i++) {
        int x = i % 400 - 100, y = i / 400 - 100, s = i % 7;
        float v = gfx_hash(x, y, s);
        in_range = in_range && v >= 0.0f && v < 1.0f;
        stable = stable && v == gfx_hash(x, y, s);
        total += (double)v;
    }
    CHECK(in_range);
    CHECK(stable);
    CHECK(total / 100000.0 > 0.49 && total / 100000.0 < 0.51);
}

int main(void)
{
    test_hsl_round_trip();
    test_ramp_shifts_hue();
    test_outline_is_dark_in_its_own_hue();
    test_snowed();
    test_soil_ramp();
    test_hash_in_range_and_stable();
    TEST_SUMMARY();
}
