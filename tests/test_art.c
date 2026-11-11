#include "test.h"

#include <string.h> /* memcmp, memcpy */

#include "art.h"
#include "sprite.h"

/* All nine traits at one value. */
static BodyTraits all_traits(float v)
{
    BodyTraits t = {v, v, v, v, v, v, v, v, v};
    return t;
}

/* The lab's hunter genes: long legs (0.5, rounded to 2/3), no other part. */
static BodyTraits hunter(void)
{
    BodyTraits t = {0.85f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f};
    return species_round(&t);
}

/* A raster with the small grid set, as skeleton_raster leaves it. */
static void small_grid(SkeletonRaster *r)
{
    r->w = SKELETON_W;
    r->h = SKELETON_H;
    r->ox = SKELETON_OX;
    r->oy = SKELETON_OY;
}

/* The wolf has frames for every action; a species without art has none. */
static void test_clips(void)
{
    BodyTraits t = hunter();
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ArtClip *clip = art_clip(SPECIES_WOLF, a, &t);
        CHECK(clip != NULL);
        CHECK(clip != NULL && clip->frames > 0);
        CHECK(art_clip(SPECIES_DEER, a, &t) == NULL);
    }
    CHECK(art_clip(SPECIES_WOLF, -1, &t) == NULL);
    CHECK(art_clip(SPECIES_WOLF, ACTION_COUNT, &t) == NULL);
    CHECK(art_scale_for(SPECIES_DEER, 30, 20) == 0.0f);
    CHECK(art_scale_for(SPECIES_WOLF, 0, 20) == 0.0f);
}

/* Plates, a horn or a fin keep the skeleton's body; long legs do not. */
static void test_parts_step_aside(void)
{
    BodyTraits t = hunter();
    t.armour = 1.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) == NULL);
    t = hunter();
    t.weapon = 2.0f / 3.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) == NULL);
    t = hunter();
    t.swim = 1.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) == NULL);
    t = hunter();
    t.legs = 1.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) != NULL);
    /* A part the frames cover does not: every set's covered genes at 1. */
    for (int s = 0; s < ART_SET_COUNT; s++) {
        BodyTraits all = hunter();
        all.armour = ART_SETS[s].covers & ART_COVERS_ARMOUR ? 1.0f : 0.0f;
        all.weapon = ART_SETS[s].covers & ART_COVERS_WEAPON ? 1.0f : 0.0f;
        all.swim = ART_SETS[s].covers & ART_COVERS_SWIM ? 1.0f : 0.0f;
        CHECK(art_clip(ART_SETS[s].species, ACTION_REST, &all) != NULL);
    }
}

/* Every frame of the generated data is in range: inks, sizes, eye. */
static void test_data_in_range(void)
{
    for (int s = 0; s < ART_SET_COUNT; s++) {
        CHECK(ART_SETS[s].stand_w > 0 && ART_SETS[s].stand_h > 0);
        for (int a = 0; a < ACTION_COUNT; a++) {
            const ArtClip *clip = &ART_SETS[s].clip[a];
            for (int i = 0; i < clip->frames; i++) {
                const ArtFrame *f = &clip->frame[i];
                CHECK(f->w > 0 && f->h > 0);
                CHECK(f->w <= SKELETON_DETAIL_W && f->h <= SKELETON_DETAIL_H);
                bool inks_ok = true;
                for (int k = 0; k < f->w * f->h; k++) {
                    inks_ok = inks_ok && f->ink[k] < INK_COUNT;
                }
                CHECK(inks_ok);
                CHECK((f->ex == -1 && f->ey == -1) ||
                      (f->ex >= 0 && f->ex < f->w && f->ey >= 0 && f->ey < f->h));
            }
        }
    }
}

/* At scale 1 the frame comes out at its own size, its lowest row on the
 * anchor; at a half, about half as wide. */
static void test_raster_scale(void)
{
    static SkeletonRaster r;
    BodyTraits t = hunter();
    const ArtClip *clip = art_clip(SPECIES_WOLF, ACTION_REST, &t);
    const ArtFrame *f = &clip->frame[0];
    small_grid(&r);
    art_raster(&r, clip, 0, 1.0f, 30.0f, 48.0f);
    CHECK(r.x1 - r.x0 == f->w);
    CHECK(r.y1 - r.y0 == f->h);
    CHECK(r.y1 - 1 == 48);  /* the standing pose's lowest row is the anchor's */
    CHECK(r.eye_x >= r.x0 && r.eye_x < r.x1);  /* the standing wolf has an eye */
    art_raster(&r, clip, 0, 0.5f, 30.0f, 48.0f);
    int w = r.x1 - r.x0;
    CHECK(w >= f->w / 2 - 2 && w <= f->w / 2 + 2);
    /* Nothing at all for a scale of 0. */
    art_raster(&r, clip, 0, 0.0f, 30.0f, 48.0f);
    CHECK(r.x1 == r.x0);
}

/* The silhouette's edge is the outline, at every scale; and the same
 * arguments give the same pixels. */
static void test_raster_outline_and_pure(void)
{
    static SkeletonRaster r;
    static uint8_t first[SKELETON_W * SKELETON_H];
    BodyTraits t = hunter();
    const float scales[] = {0.3f, 0.55f, 0.8f, 1.0f};
    for (int a = 0; a < ACTION_COUNT; a++) {
        const ArtClip *clip = art_clip(SPECIES_WOLF, a, &t);
        for (int s = 0; s < 4; s++) {
            small_grid(&r);
            art_raster(&r, clip, 1, scales[s], 30.0f, 48.0f);
            bool edges_ok = true;
            for (int y = 0; y < r.h; y++) {
                for (int x = 0; x < r.w; x++) {
                    uint8_t ink = r.ink[y * r.w + x];
                    bool edge = x == 0 || y == 0 || x == r.w - 1 || y == r.h - 1 ||
                                r.ink[y * r.w + x - 1] == INK_NONE ||
                                r.ink[y * r.w + x + 1] == INK_NONE ||
                                r.ink[(y - 1) * r.w + x] == INK_NONE ||
                                r.ink[(y + 1) * r.w + x] == INK_NONE;
                    if (ink != INK_NONE && edge && ink != INK_OUTLINE) {
                        edges_ok = false;
                    }
                }
            }
            CHECK(edges_ok);
            memcpy(first, r.ink, sizeof first);
            art_raster(&r, clip, 1, scales[s], 30.0f, 48.0f);
            CHECK(memcmp(first, r.ink, sizeof first) == 0);
        }
    }
}

/* Through the cache: art on draws the frames, off the skeleton, and turning
 * it over forgets the pictures kept. */
static void test_cache_art_switch(void)
{
    static SpriteCache c;
    CHECK(sprite_cache_init(&c, 32, 4));
    CHECK(c.art);
    BodyTraits t = all_traits(0.0f);
    t.size = 0.85f;
    t.meat = 1.0f;
    SpriteBody b = sprite_body_of(&t, SPECIES_WOLF, 2.5f, SPRITE_BASE_NORMAL);
    SpritePicture with_art = sprite_picture(&c, &b, ACTION_WALK, 0);
    int art_w = with_art.w, art_h = with_art.h;
    CHECK(with_art.eye_x >= 0);
    long builds = c.builds;
    sprite_cache_set_art(&c, true);  /* no change: nothing forgotten */
    sprite_picture(&c, &b, ACTION_WALK, 0);
    CHECK(c.builds == builds);
    sprite_cache_set_art(&c, false);
    SpritePicture drawn = sprite_picture(&c, &b, ACTION_WALK, 0);
    CHECK(c.builds == builds + 1);
    CHECK(drawn.w != art_w || drawn.h != art_h);
    /* The art's standing body covers about what the skeleton's does. */
    SpritePicture stand_drawn = sprite_picture(&c, &b, ACTION_REST, 0);
    sprite_cache_set_art(&c, true);
    SpritePicture stand_art = sprite_picture(&c, &b, ACTION_REST, 0);
    int area_drawn = stand_drawn.w * stand_drawn.h, area_art = stand_art.w * stand_art.h;
    CHECK(area_art * 4 >= area_drawn * 3 && area_art * 4 <= area_drawn * 5);
    /* Far has no eye, art or not. */
    SpriteBody far = sprite_body_of(&t, SPECIES_WOLF, 2.5f, SPRITE_BASE_FAR);
    SpritePicture pf = sprite_picture(&c, &far, ACTION_REST, 0);
    CHECK(pf.eye_x == -1 && pf.eye_y == -1);
    /* Every action and size fits its grid. */
    for (int level = 0; level < 8; level++) {
        float sizes[8] = {0.3f, 0.7f, 1.5f, 2.5f, 3.5f, 5.0f, 7.0f, 10.0f};
        SpriteBody bl = sprite_body_of(&t, SPECIES_WOLF, sizes[level], SPRITE_BASE_NORMAL);
        for (int a = 0; a < ACTION_COUNT; a++) {
            SpritePicture p = sprite_picture(&c, &bl, a, 5);
            CHECK(p.w > 0 && p.w <= SKELETON_W && p.h > 0 && p.h <= SKELETON_H);
        }
    }
    sprite_cache_free(&c);
}

int main(void)
{
    test_clips();
    test_parts_step_aside();
    test_data_in_range();
    test_raster_scale();
    test_raster_outline_and_pure();
    test_cache_art_switch();
    TEST_SUMMARY();
}
