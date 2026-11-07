#include "test.h"

#include <math.h>   /* fabsf, INFINITY, NAN */
#include <stdlib.h> /* abs */

#include "canvas.h"
#include "palette.h"
#include "ramp.h"
#include "settings.h"
#include "sprite.h"

/* A creature of the given lineage with every gene at 0.5 but those set after. */
static Creature creature_of(uint32_t lineage)
{
    Creature c = {0}; /* every field 0 or false */
    c.alive = true;
    c.lineage = lineage;
    for (int i = 0; i < GENOME_LEN; i++) {
        c.genome.g[i] = 0.5f;
    }
    return c;
}

/* All nine traits at one value. */
static BodyTraits all_traits(float v)
{
    BodyTraits t = {v, v, v, v, v, v, v, v, v};
    return t;
}

/* A canvas pixel (0xFFRRGGBB) back into a colour. */
static Colour colour_of(uint32_t pixel)
{
    Colour c = {(uint8_t)((pixel >> 16) & 0xFFu), (uint8_t)((pixel >> 8) & 0xFFu),
                (uint8_t)(pixel & 0xFFu)};
    return c;
}

/* The second time a picture is asked for it is not drawn again. */
static void test_picture_cached(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 16, 4));
    BodyTraits t = all_traits(0.5f);
    SpriteBody b = sprite_body_of(&t, SPECIES_DEER, 1.5f, SPRITE_BASE_NORMAL);
    SpritePicture p1 = sprite_picture(&c, &b, ACTION_REST, 0);
    CHECK(c.builds == 1);
    CHECK(p1.ink != NULL);
    CHECK(p1.x1 > p1.x0 && p1.y1 > p1.y0);
    SpritePicture p2 = sprite_picture(&c, &b, ACTION_REST, 0);
    CHECK(c.builds == 1);
    CHECK(p2.ink == p1.ink);
    /* Another frame is another picture. */
    sprite_picture(&c, &b, ACTION_WALK, 3);
    CHECK(c.builds == 2);
    /* So is the detailed one, kept in the large store. */
    SpriteBody d = sprite_body_of(&t, SPECIES_DEER, 1.5f, SPRITE_BASE_DETAIL);
    SpritePicture pd = sprite_picture(&c, &d, ACTION_REST, 0);
    CHECK(c.builds == 3);
    CHECK(pd.w == SKELETON_DETAIL_W && pd.h == SKELETON_DETAIL_H);
    CHECK(p1.w == SKELETON_W && p1.h == SKELETON_H);
    sprite_cache_free(&c);
    sprite_cache_free(&c); /* safe twice */
}

/* Eight entries: the ninth key takes the place of the one unused longest. */
static void test_cache_replaces_the_oldest(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 8, 1));
    BodyTraits t = all_traits(0.3f);
    SpriteBody b = sprite_body_of(&t, SPECIES_WOLF, 1.0f, SPRITE_BASE_NORMAL);
    /* Frames of the attack are eight different keys. */
    for (int f = 0; f < 8; f++) {
        sprite_picture(&c, &b, ACTION_ATTACK, f);
    }
    CHECK(c.builds == 8);
    sprite_picture(&c, &b, ACTION_ATTACK, 0); /* 0 used again: 1 is now the oldest */
    CHECK(c.builds == 8);
    sprite_picture(&c, &b, ACTION_ATTACK, 8); /* the ninth */
    CHECK(c.builds == 9);
    sprite_picture(&c, &b, ACTION_ATTACK, 0);
    for (int f = 2; f < 9; f++) {
        sprite_picture(&c, &b, ACTION_ATTACK, f);
    }
    CHECK(c.builds == 9); /* all still kept */
    sprite_picture(&c, &b, ACTION_ATTACK, 1);
    CHECK(c.builds == 10); /* 1 was the one replaced */
    sprite_cache_free(&c);
}

/* After r or a load nothing of the old world is kept. */
static void test_reset_forgets_bodies(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 16, 4));
    BodyTraits t = all_traits(0.6f);
    SpriteBody b = sprite_body_of(&t, SPECIES_LION, 2.0f, SPRITE_BASE_FAR);
    sprite_picture(&c, &b, ACTION_REST, 0);
    sprite_picture(&c, &b, ACTION_REST, 0);
    CHECK(c.builds == 1);
    sprite_cache_reset(&c);
    sprite_picture(&c, &b, ACTION_REST, 0);
    CHECK(c.builds == 2);
    sprite_picture(&c, &b, ACTION_REST, 0);
    CHECK(c.builds == 2);
    sprite_cache_free(&c);
}

/* Two members of one family with nearly the same genes are one body. */
static void test_family_shares_a_body(void)
{
    Settings set = settings_default();
    Creature a = creature_of(42), b = creature_of(42);
    a.genome.g[GENE_SIZE] = 0.40f;
    b.genome.g[GENE_SIZE] = 0.41f;
    a.genome.g[GENE_SPEED] = 0.55f;
    b.genome.g[GENE_SPEED] = 0.56f;
    for (int base = 0; base < SPRITE_BASES; base++) {
        SpriteBody ba = sprite_body(&a, &set, base);
        SpriteBody bb = sprite_body(&b, &set, base);
        CHECK(sprite_body_equal(&ba, &bb));
        CHECK(ba.base == base);
    }
    /* Another family can be another species: not asked here, as it may not. */
}

static void test_body_of_matches_body(void)
{
    Settings set = settings_default();
    for (uint32_t lineage = 1; lineage < 40; lineage++) {
        Creature c = creature_of(lineage);
        c.genome.g[GENE_MEAT] = (float)(lineage % 4) * 0.3f;
        c.genome.g[GENE_ARMOUR] = (float)(lineage % 3) * 0.45f;
        c.grown = (float)(lineage % 5) * 2.0f;
        BodyTraits t = species_traits(&c.genome);
        BodyTraits r = species_round(&t);
        float size = creature_size(&c, &set);
        int species = species_pick(c.lineage, species_role(&r), size);
        SpriteBody of = sprite_body_of(&t, species, size, SPRITE_BASE_NORMAL);
        SpriteBody body = sprite_body(&c, &set, SPRITE_BASE_NORMAL);
        CHECK(sprite_body_equal(&of, &body));
        CHECK(body.species == species);
        CHECK(body.k_level == species_k_level(size));
    }
}

/* Two hues: the same species' base colour in two hues, equally light. */
static void test_palette_takes_the_family_hue(void)
{
    SpritePalette a, b;
    sprite_palette(&a, SPECIES_DEER, 0.0f);
    sprite_palette(&b, SPECIES_DEER, 0.6f);
    float ha, sa, la, hb, sb, lb;
    ramp_to_hsl(colour_of(a.ink[INK_BASE]), &ha, &sa, &la);
    ramp_to_hsl(colour_of(b.ink[INK_BASE]), &hb, &sb, &lb);
    float apart = fabsf(ha - hb);
    apart = apart > 0.5f ? 1.0f - apart : apart; /* the short way round the wheel */
    CHECK(apart > 0.1f);
    CHECK(fabsf(la - lb) <= 0.02f);
    /* The hue is the one drawn, through render_display_hue. */
    float shown = render_display_hue(0.6f);
    float off = fabsf(hb - shown);
    off = off > 0.5f ? 1.0f - off : off;
    CHECK(off < 0.02f);
    /* The fixed inks do not follow the hue. */
    CHECK(a.ink[INK_HORN0 + 2] == b.ink[INK_HORN0 + 2]);
    CHECK(a.ink[INK_EYE] == canvas_rgb(18, 14, 22));
    CHECK(a.ink[INK_MOUTH] == canvas_rgb(70, 22, 30));
    CHECK(a.ink[INK_TEETH] == canvas_rgb(246, 240, 226));
}

static void test_palette_look_is_the_species_colour(void)
{
    SpritePalette p;
    sprite_palette_look(&p, SPECIES_DEER);
    Colour base = colour_of(p.ink[INK_BASE]);
    CHECK(abs(base.r - 0xb0) <= 2);
    CHECK(abs(base.g - 0x87) <= 2);
    CHECK(abs(base.b - 0x5a) <= 2);
    /* A flash of 1 is white, of 0 the colour itself. */
    SpritePalette white, same;
    sprite_palette_flash(&white, &p, 1.0f);
    sprite_palette_flash(&same, &p, 0.0f);
    CHECK(white.ink[INK_BASE] == canvas_rgb(255, 255, 255));
    CHECK(same.ink[INK_BASE] == p.ink[INK_BASE]);
}

/* Scars land on the body itself, never on its outline or beside it. */
static void test_scar_pixels_on_body(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 16, 4));
    for (int species = 0; species < SPECIES_COUNT; species++) {
        BodyTraits t = all_traits(0.5f);
        SpriteBody b = sprite_body_of(&t, species, 4.0f, SPRITE_BASE_NORMAL);
        SpritePicture p = sprite_picture(&c, &b, ACTION_REST, 0);
        int16_t px[64][2];
        CHECK(sprite_scar_pixels(&p, 0, px, 64) == 0);
        int one = sprite_scar_pixels(&p, 1, px, 64);
        int three = sprite_scar_pixels(&p, 3, px, 64);
        CHECK(one > 0);
        CHECK(three > one);
        CHECK(sprite_scar_pixels(&p, 9, px, 64) == three); /* at most 3 */
        for (int k = 0; k < three; k++) {
            int x = px[k][0], y = px[k][1];
            CHECK(x >= p.x0 && x < p.x1 && y >= p.y0 && y < p.y1);
            uint8_t ink = p.ink[y * p.w + x];
            CHECK(ink != INK_NONE && ink != INK_OUTLINE);
        }
        /* Room for fewer than there are: no more than max are written. */
        CHECK(sprite_scar_pixels(&p, 3, px, 2) == 2);
    }
    sprite_cache_free(&c);
}

/* The largest body of every species fits its grid at every base: Normal within
 * three tiles, Far about half of it, the detailed one 1.6 times, clear of the
 * large grid's edge. */
static void test_cap_keeps_giants_inside(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 16, 4));
    for (int species = 0; species < SPECIES_COUNT; species++) {
        for (int v = 0; v <= 1; v++) {
            BodyTraits t = all_traits((float)v);
            int w[SPRITE_BASES], h[SPRITE_BASES];
            for (int base = 0; base < SPRITE_BASES; base++) {
                SpriteBody b = sprite_body_of(&t, species, 1000000.0f, base);
                CHECK(b.k_level == SPECIES_K_LEVELS - 1);
                SpritePicture p = sprite_picture(&c, &b, ACTION_REST, 0);
                w[base] = p.x1 - p.x0;
                h[base] = p.y1 - p.y0;
                if (base == SPRITE_BASE_DETAIL) {
                    CHECK(p.x0 >= 1 && p.y0 >= 1 && p.x1 <= p.w - 1 && p.y1 <= p.h - 1);
                }
            }
            CHECK(w[SPRITE_BASE_NORMAL] <= SPRITE_MAX_W + 2);
            CHECK(h[SPRITE_BASE_NORMAL] <= SPRITE_MAX_H + 2);
            CHECK(abs(2 * w[SPRITE_BASE_FAR] - w[SPRITE_BASE_NORMAL]) <= 6);
            CHECK((float)w[SPRITE_BASE_DETAIL] <= 1.6f * (float)SPRITE_MAX_W + 2.0f);
            CHECK((float)h[SPRITE_BASE_DETAIL] <= 1.6f * (float)SPRITE_MAX_H + 2.0f);
        }
    }
    sprite_cache_free(&c);
}

/* A frame past the cycle's end comes round again; a newborn's level 0 is
 * smaller than a grown body. */
static void test_frames_wrap_and_levels_grow(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 16, 4));
    BodyTraits t = all_traits(0.5f);
    SpriteBody b = sprite_body_of(&t, SPECIES_ZEBRA, 1.5f, SPRITE_BASE_NORMAL);
    sprite_picture(&c, &b, ACTION_SLEEP, 1);
    long before = c.builds;
    sprite_picture(&c, &b, ACTION_SLEEP, 1 + SPRITE_FRAMES[ACTION_SLEEP]);
    CHECK(c.builds == before);
    SpriteBody small = b, large = b;
    small.k_level = 0;
    large.k_level = SPECIES_K_LEVELS - 1;
    CHECK(sprite_k(&small) < sprite_k(&large));
    SpritePicture ps = sprite_picture(&c, &small, ACTION_REST, 0);
    int small_w = ps.x1 - ps.x0;
    SpritePicture pl = sprite_picture(&c, &large, ACTION_REST, 0);
    CHECK(pl.x1 - pl.x0 > small_w);
    const int frames[ACTION_COUNT] = {24, 24, 16, 30, 39, 54, 6};
    for (int a = 0; a < ACTION_COUNT; a++) {
        CHECK(SPRITE_FRAMES[a] == frames[a]);
    }
    sprite_cache_free(&c);
}

/* The steps go up with size and never down. */
static void test_tiles_step_up_with_size(void)
{
    CHECK(sprite_tiles_for(0.5f) == 1);
    CHECK(sprite_tiles_for(1.5f) == 1);
    CHECK(sprite_tiles_for(SPRITE_TWO_TILES) == 2);
    CHECK(sprite_tiles_for(SPRITE_THREE_TILES) == 3);
    int last = 1;
    for (int k = 0; k <= 400; k++) {
        int tiles = sprite_tiles_for((float)k * 0.05f);
        CHECK(tiles >= last);
        last = tiles;
    }
    CHECK(last == 3);
}

static void test_tiles_capped_for_huge(void)
{
    CHECK(sprite_tiles_for(1000000.0f) == 3);
    CHECK(sprite_tiles_for(INFINITY) == 3);
    CHECK(sprite_tiles_for(NAN) == 1);
    CHECK(sprite_tiles_for(-5.0f) == 1);
}

int main(void)
{
    test_picture_cached();
    test_cache_replaces_the_oldest();
    test_reset_forgets_bodies();
    test_family_shares_a_body();
    test_body_of_matches_body();
    test_palette_takes_the_family_hue();
    test_palette_look_is_the_species_colour();
    test_scar_pixels_on_body();
    test_cap_keeps_giants_inside();
    test_frames_wrap_and_levels_grow();
    test_tiles_step_up_with_size();
    test_tiles_capped_for_huge();
    TEST_SUMMARY();
}
