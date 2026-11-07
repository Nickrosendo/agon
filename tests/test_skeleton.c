#include "test.h"

#include <math.h>   /* fabsf, sqrtf, floorf */
#include <string.h> /* memcmp */

#include "skeleton.h"
#include "species.h"

/* A raster is about 22 KB: static, so it lives outside the stack and starts
 * zeroed. Every test reuses these two. */
static SkeletonRaster raster_a, raster_b;

/* The mockup's role genes (v9:82-86), rounded as the drawing rounds them. */
static BodyTraits role_traits(int role)
{
    BodyTraits raw = {0.5f, 0.4f, 0.35f, 1.0f, 0.0f, 0.0f, 0.0f, 0.55f, 0.0f};
    if (role == ROLE_RUNNER) {
        BodyTraits r = {0.25f, 1.0f, 0.5f, 0.5f, 0.2f, 0.0f, 0.0f, 1.0f, 0.0f};
        raw = r;
    } else if (role == ROLE_HUNTER) {
        BodyTraits r = {0.85f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f};
        raw = r;
    } else if (role == ROLE_WATCHER) {
        BodyTraits r = {0.3f, 0.4f, 1.0f, 0.6f, 0.2f, 0.0f, 0.0f, 0.35f, 0.0f};
        raw = r;
    } else if (role == ROLE_ARMOURED) {
        BodyTraits r = {0.8f, 0.2f, 0.3f, 0.8f, 0.1f, 1.0f, 0.0f, 0.3f, 0.0f};
        raw = r;
    } else if (role == ROLE_SWIMMER) {
        BodyTraits r = {0.5f, 0.4f, 0.3f, 0.6f, 0.2f, 0.0f, 0.0f, 0.3f, 1.0f};
        raw = r;
    }
    return species_round(&raw);
}

/* Corner number c (0..63) of the six traits a skeleton reads: trait i
 * (size, speed, sight, plant, meat, legs) is 1 when bit i of c is set, else 0.
 * Armour, weapon and swim stay 0: test_other_traits_change_nothing shows they
 * change no pixel here (task 7's horns and shell will read the first two). */
static BodyTraits corner(int c)
{
    float v[6];
    for (int i = 0; i < 6; i++) {
        v[i] = ((c >> i) & 1) ? 1.0f : 0.0f;
    }
    BodyTraits t = {v[0], v[1], v[2], v[3], v[4], 0.0f, 0.0f, v[5], 0.0f};
    return t;
}

/* Each species' cap, worked out once: it takes a few hundred rasters. */
static float caps[SPECIES_COUNT];

static float cap_of(int species)
{
    if (caps[species] == 0.0f) {
        caps[species] = skeleton_k_cap(species);
    }
    return caps[species];
}

/* K of the largest size level, as the world draws it: capped to fit. */
static float largest_k(int species)
{
    float k = SPECIES_K[SPECIES_K_LEVELS - 1] * SPECIES[species].scale;
    float cap = cap_of(species);
    return k < cap ? k : cap;
}

static void raster_species(SkeletonRaster *r, int species, float k,
                           const SpritePose *p, bool marks, bool large)
{
    Anatomy a;
    BodyTraits t = role_traits(SPECIES[species].role);
    species_anatomy(&a, species, &t, k);
    skeleton_raster(r, &a, p, marks, large);
}

static void raster_standing(SkeletonRaster *r, int species, float k, bool large)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    raster_species(r, species, k, &p, true, large);
}

static int count_ink(const SkeletonRaster *r, int ink)
{
    int n = 0;
    for (int i = 0; i < r->w * r->h; i++) {
        n += r->ink[i] == ink;
    }
    return n;
}

/* Distance of the knee from the straight line hip to foot, in pixels. */
static float knee_off_line(const SkeletonRaster *r, int leg)
{
    float hx = r->hip[leg][0], hy = r->hip[leg][1];
    float dx = r->foot[leg][0] - hx, dy = r->foot[leg][1] - hy;
    float len = sqrtf(dx * dx + dy * dy);
    float kx = r->knee[leg][0] - hx, ky = r->knee[leg][1] - hy;
    return fabsf(kx * dy - ky * dx) / len; /* the cross product over the length */
}

static bool ring_is_empty(const SkeletonRaster *r)
{
    for (int y = 0; y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            bool edge = x == 0 || y == 0 || x == r->w - 1 || y == r->h - 1;
            if (edge && r->ink[y * r->w + x] != INK_NONE) {
                return false;
            }
        }
    }
    return true;
}

static void test_same_input_same_pixels(void)
{
    for (int s = 0; s < SPECIES_COUNT; s++) {
        raster_standing(&raster_a, s, largest_k(s), false);
        raster_standing(&raster_b, s, largest_k(s), false);
        CHECK(memcmp(raster_a.part, raster_b.part, sizeof raster_a.part) == 0);
        CHECK(memcmp(raster_a.ink, raster_b.ink, sizeof raster_a.ink) == 0);
        CHECK(raster_a.eye_x == raster_b.eye_x && raster_a.eye_y == raster_b.eye_y);
        CHECK(count_ink(&raster_a, INK_NONE) < raster_a.w * raster_a.h);
    }
}

static void test_everything_inside_the_grid(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    for (int s = 0; s < SPECIES_COUNT; s++) {
        float cap = cap_of(s);
        CHECK(cap > 0.3f);
        bool small_ok = true, large_ok = true, width_ok = true;
        for (int c = 0; c < 64; c++) {
            Anatomy a;
            BodyTraits t = corner(c);
            species_anatomy(&a, s, &t, cap);
            skeleton_raster(&raster_a, &a, &p, true, false);
            small_ok = small_ok && ring_is_empty(&raster_a);
            width_ok = width_ok && raster_a.x1 - raster_a.x0 <= SPRITE_MAX_W &&
                       raster_a.y1 - raster_a.y0 <= SPRITE_MAX_H;
            species_anatomy(&a, s, &t, cap * 1.6f);
            skeleton_raster(&raster_a, &a, &p, true, true);
            large_ok = large_ok && ring_is_empty(&raster_a);
        }
        if (!small_ok || !large_ok || !width_ok) {
            fprintf(stderr, "  species %s, cap %.3f\n", SPECIES[s].name, (double)cap);
        }
        CHECK(small_ok);
        CHECK(large_ok);
        CHECK(width_ok);
    }
}

static void test_other_traits_change_nothing(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    for (int s = 0; s < SPECIES_COUNT; s++) {
        Anatomy a;
        BodyTraits t = corner(63);
        species_anatomy(&a, s, &t, largest_k(s));
        skeleton_raster(&raster_a, &a, &p, true, false);
        t.armour = t.weapon = t.swim = 1.0f;
        species_anatomy(&a, s, &t, largest_k(s));
        skeleton_raster(&raster_b, &a, &p, true, false);
        CHECK(memcmp(raster_a.ink, raster_b.ink, sizeof raster_a.ink) == 0);
    }
}

/* The near legs (2 and 3) of a standing deer and elephant: straight, and the
 * column under the hip is leg all the way down. */
static void test_planted_leg_is_straight(void)
{
    const int species[2] = {SPECIES_DEER, SPECIES_ELEPHANT};
    for (int i = 0; i < 2; i++) {
        SkeletonRaster *r = &raster_a;
        raster_standing(r, species[i], largest_k(species[i]), false);
        for (int leg = 2; leg < 4; leg++) {
            CHECK(knee_off_line(r, leg) < 1.0f);
            int x = (int)floorf(r->hip[leg][0]);
            int top = (int)floorf(r->hip[leg][1]);
            int bottom = (int)floorf(r->foot[leg][1]) - 1; /* the foot point is the ground line */
            bool unbroken = bottom > top;
            for (int y = top; y <= bottom; y++) {
                unbroken = unbroken && r->part[y * r->w + x] == PART_LEG;
            }
            CHECK(unbroken);
        }
    }
}

static void test_lifted_leg_bends(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    p.action = ACTION_WALK;
    /* The near hind leg (number 2) has no phase offset: 0.8 of a turn puts it
     * half way through its swing, its foot at the top of its arc. */
    p.phase = 0.8f * 6.2831853f;
    raster_species(&raster_a, SPECIES_DEER, largest_k(SPECIES_DEER), &p, true, false);
    CHECK(raster_a.foot[2][1] < (float)raster_a.oy - 0.5f); /* lifted */
    CHECK(knee_off_line(&raster_a, 2) > 1.0f);
    raster_species(&raster_a, SPECIES_ELEPHANT, largest_k(SPECIES_ELEPHANT), &p,
                   true, false);
    for (int leg = 0; leg < 4; leg++) {
        CHECK(knee_off_line(&raster_a, leg) < 1.0f);
    }
}

static void test_shape_list_fits(void)
{
    for (int s = 0; s < SPECIES_COUNT; s++) {
        raster_standing(&raster_a, s, largest_k(s), false);
        CHECK(raster_a.shapes > 10 && raster_a.shapes <= SKELETON_SHAPES_MAX);
        raster_standing(&raster_a, s, largest_k(s) * 1.6f, true);
        CHECK(raster_a.shapes > 10 && raster_a.shapes <= SKELETON_SHAPES_MAX);
    }
}

/* The centre of the PART_BODY pixels, as an offset from the ground point. */
static float body_centre_dx(const SkeletonRaster *r)
{
    int lo = r->w, hi = -1;
    for (int i = 0; i < r->w * r->h; i++) {
        if (r->part[i] == PART_BODY) {
            int x = i % r->w;
            lo = x < lo ? x : lo;
            hi = x > hi ? x : hi;
        }
    }
    return (float)(lo + hi + 1) * 0.5f - (float)r->ox;
}

static void test_large_grid_keeps_the_ground_point(void)
{
    for (int s = 0; s < SPECIES_COUNT; s++) {
        float k = largest_k(s);
        raster_standing(&raster_a, s, k, false);
        raster_standing(&raster_b, s, k * 1.6f, true);
        CHECK(raster_a.w == SKELETON_W && raster_a.oy == SKELETON_OY);
        CHECK(raster_b.w == SKELETON_DETAIL_W && raster_b.oy == SKELETON_DETAIL_OY);
        /* The lowest ink is the outline under the feet, on the ground row.
         * Pillar legs end in the mockup's round cap of 0.72 leg widths, 0.4 K
         * above the ground (v9:283), so they dip below it by a few pixels: by
         * the same amount, scaled, on both grids. */
        int below_small = raster_a.y1 - 1 - raster_a.oy;
        int below_large = raster_b.y1 - 1 - raster_b.oy;
        if (SPECIES[s].pillar) {
            CHECK(below_small > 0 && below_small <= 4);
            CHECK(fabsf((float)below_large - 1.6f * (float)below_small) <= 1.5f);
        } else {
            CHECK(below_small == 0);
            CHECK(below_large == 0);
        }
        CHECK(fabsf(body_centre_dx(&raster_b) - 1.6f * body_centre_dx(&raster_a)) <= 2.0f);
    }
}

static void test_crouch_bends_the_knees(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    p.crouch = 1.2f;
    raster_species(&raster_a, SPECIES_WOLF, largest_k(SPECIES_WOLF), &p, true, false);
    CHECK(knee_off_line(&raster_a, 0) > 1.0f);
    CHECK(knee_off_line(&raster_a, 2) > 1.0f);
}

/* The clean-up's rule, checked on the finished picture. */
static int lone_shade_pixels(const SkeletonRaster *r)
{
    int lone = 0;
    for (int y = 1; y < r->h - 1; y++) {
        for (int x = 1; x < r->w - 1; x++) {
            int i = y * r->w + x, v = r->ink[i];
            if (v < INK_DEEP || v > INK_HIGH) {
                continue;
            }
            const int at[4] = {i + 1, i - 1, i + r->w, i - r->w};
            bool all_same = true;
            for (int n = 0; n < 4; n++) {
                all_same = all_same && r->part[at[n]] == r->part[i] &&
                           r->ink[at[n]] != v && r->ink[at[n]] == r->ink[at[0]];
            }
            lone += all_same;
        }
    }
    return lone;
}

static void test_no_lone_shade_pixel(void)
{
    SpritePose walk;
    skeleton_pose_standing(&walk);
    walk.action = ACTION_WALK;
    walk.phase = 1.3f;
    for (int s = 0; s < SPECIES_COUNT; s++) {
        raster_standing(&raster_a, s, largest_k(s), false);
        CHECK(lone_shade_pixels(&raster_a) == 0);
        raster_standing(&raster_a, s, largest_k(s) * 1.6f, true);
        CHECK(lone_shade_pixels(&raster_a) == 0);
        raster_species(&raster_a, s, largest_k(s), &walk, true, false);
        CHECK(lone_shade_pixels(&raster_a) == 0);
    }
}

static bool filled(const SkeletonRaster *r, int x, int y)
{
    return x >= 0 && y >= 0 && x < r->w && y < r->h && r->part[y * r->w + x] != PART_NONE;
}

static void test_outline_touches_body_and_softens_on_top(void)
{
    for (int s = 0; s < SPECIES_COUNT; s++) {
        SkeletonRaster *r = &raster_a;
        raster_standing(r, s, largest_k(s), false);
        int soft = 0;
        bool ok = true;
        for (int y = 0; y < r->h; y++) {
            for (int x = 0; x < r->w; x++) {
                int i = y * r->w + x;
                if (r->part[i] != PART_NONE) {
                    continue;
                }
                bool touches = filled(r, x + 1, y) || filled(r, x - 1, y) ||
                               filled(r, x, y + 1) || filled(r, x, y - 1);
                bool lit_below = false, mark_below = false;
                if (filled(r, x, y + 1)) {
                    int below = r->ink[i + r->w];
                    lit_below = below == INK_LIGHT || below == INK_HIGH || below == INK_BELLY;
                    mark_below = below == INK_NOSE || below == INK_TEETH;
                }
                int want = !touches ? INK_NONE : lit_below ? INK_SHADOW : INK_OUTLINE;
                /* A nose or a tooth is plotted over whatever is there, after
                 * the outline: on an outline pixel, or under one, whose shade
                 * then came from the ink the mark covered. */
                bool mark = r->ink[i] == INK_NOSE || r->ink[i] == INK_TEETH;
                bool either = mark_below && (r->ink[i] == INK_OUTLINE || r->ink[i] == INK_SHADOW);
                ok = ok && (mark || either || r->ink[i] == want);
                soft += r->ink[i] == INK_SHADOW;
            }
        }
        CHECK(ok);
        CHECK(soft > 0); /* the back is lit from above, so its outline is soft */
    }
}

static void test_jaw_opens(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    p.jaw = 0.85f;
    raster_species(&raster_a, SPECIES_WOLF, largest_k(SPECIES_WOLF), &p, true, false);
    CHECK(count_ink(&raster_a, INK_MOUTH) > 0);
    p.jaw = 0.0f;
    raster_species(&raster_a, SPECIES_WOLF, largest_k(SPECIES_WOLF), &p, true, false);
    CHECK(count_ink(&raster_a, INK_MOUTH) == 0);
}

static void test_far_silhouette_has_no_marks(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    p.jaw = 0.85f; /* open, so a hunter shows its teeth */
    raster_species(&raster_a, SPECIES_WOLF, largest_k(SPECIES_WOLF), &p, true, false);
    CHECK(raster_a.eye_x >= 0 && raster_a.eye_y >= 0);
    CHECK(count_ink(&raster_a, INK_NOSE) == 1);
    CHECK(count_ink(&raster_a, INK_TEETH) > 0);
    raster_species(&raster_a, SPECIES_WOLF, largest_k(SPECIES_WOLF), &p, false, false);
    CHECK(raster_a.eye_x == -1 && raster_a.eye_y == -1);
    CHECK(count_ink(&raster_a, INK_NOSE) == 0);
    CHECK(count_ink(&raster_a, INK_TEETH) == 0);
    CHECK(count_ink(&raster_a, INK_EYE) == 0);
}

int main(void)
{
    test_same_input_same_pixels();
    test_everything_inside_the_grid();
    test_other_traits_change_nothing();
    test_planted_leg_is_straight();
    test_lifted_leg_bends();
    test_shape_list_fits();
    test_large_grid_keeps_the_ground_point();
    test_crouch_bends_the_knees();
    test_no_lone_shade_pixel();
    test_outline_touches_body_and_softens_on_top();
    test_jaw_opens();
    test_far_silhouette_has_no_marks();
    TEST_SUMMARY();
}
