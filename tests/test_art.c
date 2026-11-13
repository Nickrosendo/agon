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

/* Every species has frames for every action; a species number with no art
 * set (there is none among the seventeen now, so one past them) has none. */
static void test_clips(void)
{
    BodyTraits t = hunter();
    for (int sp = 0; sp < SPECIES_COUNT; sp++) {
        for (int a = 0; a < ACTION_COUNT; a++) {
            const ArtClip *clip = art_clip(sp, a, &t);
            CHECK(clip != NULL && clip->frames > 0);
        }
        CHECK(art_scale_for(sp, 30, 20) > 0.0f);
    }
    for (int a = 0; a < ACTION_COUNT; a++) {
        CHECK(art_clip(SPECIES_COUNT, a, &t) == NULL);
    }
    CHECK(art_clip(SPECIES_WOLF, -1, &t) == NULL);
    CHECK(art_clip(SPECIES_WOLF, ACTION_COUNT, &t) == NULL);
    CHECK(art_scale_for(SPECIES_COUNT, 30, 20) == 0.0f);
    CHECK(art_scale_for(SPECIES_WOLF, 0, 20) == 0.0f);
}

/* Plates, a horn, a fin or long legs keep the species' frames: a part the
 * frames do not show is painted over them as a layer, never by falling back
 * to the skeleton's body. */
static void test_parts_keep_frames(void)
{
    BodyTraits t = hunter();
    t.armour = 1.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) != NULL);
    t = hunter();
    t.weapon = 2.0f / 3.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) != NULL);
    t = hunter();
    t.swim = 1.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) != NULL);
    t = hunter();
    t.legs = 1.0f;
    CHECK(art_clip(SPECIES_WOLF, ACTION_REST, &t) != NULL);
    BodyTraits all = all_traits(1.0f);
    for (int sp = 0; sp < SPECIES_COUNT; sp++) {
        CHECK(art_clip(sp, ACTION_WALK, &all) != NULL);
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
    art_raster(&r, clip, NULL, 0, 1.0f, 30.0f, 48.0f);
    CHECK(r.x1 - r.x0 == f->w);
    CHECK(r.y1 - r.y0 == f->h);
    CHECK(r.y1 - 1 == 48);  /* the standing pose's lowest row is the anchor's */
    CHECK(r.eye_x >= r.x0 && r.eye_x < r.x1);  /* the standing wolf has an eye */
    art_raster(&r, clip, NULL, 0, 0.5f, 30.0f, 48.0f);
    int w = r.x1 - r.x0;
    CHECK(w >= f->w / 2 - 2 && w <= f->w / 2 + 2);
    /* Nothing at all for a scale of 0. */
    art_raster(&r, clip, NULL, 0, 0.0f, 30.0f, 48.0f);
    CHECK(r.x1 == r.x0);
}

/* The silhouette's fur edge is the outline, at every scale; and the same
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
            art_raster(&r, clip, NULL, 1, scales[s], 30.0f, 48.0f);
            bool edges_ok = true;
            for (int y = 0; y < r.h; y++) {
                for (int x = 0; x < r.w; x++) {
                    uint8_t ink = r.ink[y * r.w + x];
                    bool edge = x == 0 || y == 0 || x == r.w - 1 || y == r.h - 1 ||
                                r.ink[y * r.w + x - 1] == INK_NONE ||
                                r.ink[y * r.w + x + 1] == INK_NONE ||
                                r.ink[(y - 1) * r.w + x] == INK_NONE ||
                                r.ink[(y + 1) * r.w + x] == INK_NONE;
                    bool fur = ink >= INK_DEEP && ink <= INK_BELLY;
                    if (fur && edge) {
                        edges_ok = false;
                    }
                }
            }
            CHECK(edges_ok);
            memcpy(first, r.ink, sizeof first);
            art_raster(&r, clip, NULL, 1, scales[s], 30.0f, 48.0f);
            CHECK(memcmp(first, r.ink, sizeof first) == 0);
        }
    }
}

/* ---- Layers, on hand-made frames ---- */

/* A 4 x 4 block of base fur, its anchor at the bottom row's third pixel. */
static const uint8_t BLOCK_INK[16] = {
    INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE,
    INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE, INK_BASE,
};
static const ArtFrame BLOCK_FRAME = {4, 4, 2, 3, -1, -1, BLOCK_INK};
static const ArtClip BLOCK = {1, &BLOCK_FRAME};

/* A horn one pixel wide and two tall, standing on the block's top row above
 * the anchor's column: the layer's own pixel (0, 0) is the block's (2, -2),
 * so the block's anchor (2, 3) is (0, 5) in the layer's pixels. */
static const uint8_t HORN_INK[2] = {INK_HORN0 + 2, INK_HORN0 + 2};
static const ArtFrame HORN_FRAME = {1, 2, 0, 5, -1, -1, HORN_INK};
static const ArtClip HORN = {1, &HORN_FRAME};

/* One plate on the block's pixel (2, 1), and a horn pixel on the same spot. */
static const uint8_t PLATE_INK[1] = {INK_SHELL1 + 1};
static const ArtFrame PLATE_FRAME = {1, 1, 0, 2, -1, -1, PLATE_INK};
static const ArtClip PLATE = {1, &PLATE_FRAME};
static const uint8_t SPOT_INK[1] = {INK_HORN0 + 4};
static const ArtFrame SPOT_FRAME = {1, 1, 0, 2, -1, -1, SPOT_INK};
static const ArtClip SPOT = {1, &SPOT_FRAME};

/* A layer is drawn over the frame, sticks out above it, and the box grows. */
static void test_layer_drawn_on_top(void)
{
    static SkeletonRaster r;
    small_grid(&r);
    const ArtClip *layers[ART_PART_COUNT] = {NULL, NULL, &HORN};
    art_raster(&r, &BLOCK, layers, 0, 1.0f, 30.0f, 48.0f);
    /* The block covers grid x 28..31, y 45..48; the horn x 30, y 43..44. */
    CHECK(r.ink[43 * r.w + 30] == INK_HORN0 + 2);
    CHECK(r.ink[44 * r.w + 30] == INK_HORN0 + 2);
    CHECK(r.part[43 * r.w + 30] == PART_HORN);
    CHECK(r.y0 == 43 && r.y1 == 49 && r.x0 == 28 && r.x1 == 32);
    /* Without the layer the box starts at the block's top. */
    art_raster(&r, &BLOCK, NULL, 0, 1.0f, 30.0f, 48.0f);
    CHECK(r.y0 == 45);
}

/* Shrunk to a quarter, the horn is half a grid pixel tall: a frame pixel
 * would be dropped (fewer than a third of the samples), a horn pixel is
 * kept. */
static void test_layer_any_sample(void)
{
    static SkeletonRaster r;
    small_grid(&r);
    const ArtClip *layers[ART_PART_COUNT] = {NULL, NULL, &HORN};
    art_raster(&r, &BLOCK, layers, 0, 0.25f, 30.0f, 48.0f);
    int horn = 0;
    for (int i = 0; i < r.w * r.h; i++) {
        horn += r.part[i] == PART_HORN;
    }
    CHECK(horn >= 1);
    /* Armour is broad and keeps the frame's rule: one plate pixel shrunk to
     * a quarter is gone. */
    const ArtClip *plate[ART_PART_COUNT] = {&PLATE, NULL, NULL};
    art_raster(&r, &BLOCK, plate, 0, 0.25f, 30.0f, 48.0f);
    int shell = 0;
    for (int i = 0; i < r.w * r.h; i++) {
        shell += r.part[i] == PART_SHELL;
    }
    CHECK(shell == 0);
}

/* Armour and a horn on the same pixel: the horn, drawn last, wins; the plate
 * alone is armour, marked as shell. */
static void test_layer_order(void)
{
    static SkeletonRaster r;
    small_grid(&r);
    const ArtClip *plate_only[ART_PART_COUNT] = {&PLATE, NULL, NULL};
    art_raster(&r, &BLOCK, plate_only, 0, 1.0f, 30.0f, 48.0f);
    CHECK(r.ink[46 * r.w + 30] == INK_SHELL1 + 1);
    CHECK(r.part[46 * r.w + 30] == PART_SHELL);
    const ArtClip *both[ART_PART_COUNT] = {&PLATE, NULL, &SPOT};
    art_raster(&r, &BLOCK, both, 0, 1.0f, 30.0f, 48.0f);
    CHECK(r.ink[46 * r.w + 30] == INK_HORN0 + 4);
    CHECK(r.part[46 * r.w + 30] == PART_HORN);
}

/* art_layers on the real sets: nothing under PART_FROM, nothing for a part
 * the frames cover, nothing for a bad action; every slot NULL then. */
static void test_art_layers_pick(void)
{
    const ArtClip *out[ART_PART_COUNT];
    BodyTraits none = all_traits(0.0f);
    CHECK(art_layers(SPECIES_OTTER, ACTION_REST, &none, out) == 0);
    CHECK(out[0] == NULL && out[1] == NULL && out[2] == NULL);
    BodyTraits all = all_traits(1.0f);
    art_layers(SPECIES_ARMADILLO, ACTION_REST, &all, out);
    CHECK(out[ART_PART_ARMOUR] == NULL);  /* its frames are all shell */
    art_layers(SPECIES_OTTER, ACTION_REST, &all, out);
    CHECK(out[ART_PART_SWIM] == NULL);    /* its frames are a swimmer's */
    CHECK(art_layers(SPECIES_OTTER, ACTION_COUNT, &all, out) == 0);
    /* An otter with plates and a horn gets both layers; with a medium gene
     * (2/3) too, from the full layers, since no medium ones are painted yet. */
    BodyTraits armed = all_traits(0.0f);
    armed.armour = 1.0f;
    armed.weapon = 1.0f;
    CHECK(art_layers(SPECIES_OTTER, ACTION_WALK, &armed, out) == 2);
    CHECK(out[ART_PART_ARMOUR] != NULL && out[ART_PART_WEAPON] != NULL);
    const ArtClip *full_armour = out[ART_PART_ARMOUR];
    armed.armour = 2.0f / 3.0f;
    art_layers(SPECIES_OTTER, ACTION_WALK, &armed, out);
    CHECK(out[ART_PART_ARMOUR] == full_armour);
    /* A gene of 1/3 shows no part. */
    armed.armour = 1.0f / 3.0f;
    art_layers(SPECIES_OTTER, ACTION_WALK, &armed, out);
    CHECK(out[ART_PART_ARMOUR] == NULL);
    /* A swimming wolf keeps its own body: swim layers are never painted
     * (Nicolas, 2026-11-13; the gene only changes what steps cost). */
    BodyTraits swimmer = all_traits(0.0f);
    swimmer.swim = 1.0f;
    CHECK(art_layers(SPECIES_WOLF, ACTION_RUN, &swimmer, out) == 0);
    for (int s = 0; s < ART_SET_COUNT; s++) {
        CHECK(ART_SETS[s].layer[ART_PART_SWIM][ART_SIZE_MEDIUM] == NULL);
        CHECK(ART_SETS[s].layer[ART_PART_SWIM][ART_SIZE_FULL] == NULL);
    }
    CHECK(art_layers(SPECIES_COUNT, ACTION_REST, &all, out) == 0);
}

/* Every painted layer has its base clip's frame count, frame N over frame N,
 * and its frames are in range like the base frames. */
static void test_layers_match_clips(void)
{
    for (int s = 0; s < ART_SET_COUNT; s++) {
        for (int p = 0; p < ART_PART_COUNT; p++) {
            for (int z = 0; z < ART_SIZE_COUNT; z++) {
                const ArtClip *clips = ART_SETS[s].layer[p][z];
                for (int a = 0; clips != NULL && a < ACTION_COUNT; a++) {
                    CHECK(clips[a].frames == ART_SETS[s].clip[a].frames);
                    for (int i = 0; i < clips[a].frames; i++) {
                        const ArtFrame *f = &clips[a].frame[i];
                        CHECK(f->w > 0 && f->h > 0 && f->ex == -1 && f->ey == -1);
                        bool inks_ok = true;
                        for (int k = 0; k < f->w * f->h; k++) {
                            inks_ok = inks_ok && f->ink[k] < INK_COUNT;
                        }
                        CHECK(inks_ok);
                    }
                }
            }
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
    test_parts_keep_frames();
    test_data_in_range();
    test_raster_scale();
    test_raster_outline_and_pure();
    test_layer_drawn_on_top();
    test_layer_any_sample();
    test_layer_order();
    test_art_layers_pick();
    test_layers_match_clips();
    test_cache_art_switch();
    TEST_SUMMARY();
}
