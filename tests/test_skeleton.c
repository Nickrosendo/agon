#include "test.h"

#include <math.h>   /* fabsf, sqrtf, floorf, ceilf, fmaxf */
#include <string.h> /* memcmp, memcpy */

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

/* Corner number c of the traits a skeleton reads: trait i (size, speed,
 * sight, plant, meat, legs) is 1 when bit i of c is set, else 0; bit 6 sets
 * armour and weapon together (no species has both a shell and horns, so
 * this covers both values of the one it reads). Swim stays 0:
 * test_other_traits_change_nothing shows it changes no pixel. */
static BodyTraits corner(int c)
{
    float v[7];
    for (int i = 0; i < 7; i++) {
        v[i] = ((c >> i) & 1) ? 1.0f : 0.0f;
    }
    BodyTraits t = {v[0], v[1], v[2], v[3], v[4], v[6], v[6], v[5], 0.0f};
    return t;
}

/* Horns, antlers and tusks grow with weapon, the shell with armour; a species
 * with none of these does not read either trait. */
static bool reads_weapon_or_armour(int species)
{
    unsigned parts = FEATURE_ANTLERS | FEATURE_CURL_HORNS | FEATURE_LYRE_HORNS |
                     FEATURE_RHINO_HORN | FEATURE_TUSKS | FEATURE_SHELL;
    return (SPECIES[species].features & parts) != 0;
}

/* How many corners matter for a species: 128 with armour and weapon, or the
 * 64 of the first six traits when it reads neither. */
static int corners_of(int species)
{
    return reads_weapon_or_armour(species) ? 128 : 64;
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
        for (int c = 0; c < corners_of(s); c++) {
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

/* Swim changes no pixel of any species; weapon and armour none of a species
 * without horns, tusks or a shell. */
static void test_other_traits_change_nothing(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    for (int s = 0; s < SPECIES_COUNT; s++) {
        Anatomy a;
        BodyTraits t = corner(63);
        species_anatomy(&a, s, &t, largest_k(s));
        skeleton_raster(&raster_a, &a, &p, true, false);
        /* corner() sets armour and weapon together: fine while no species
         * has both a shell and horns or tusks. */
        unsigned horns = FEATURE_ANTLERS | FEATURE_CURL_HORNS | FEATURE_LYRE_HORNS |
                         FEATURE_RHINO_HORN | FEATURE_TUSKS;
        CHECK((SPECIES[s].features & horns) == 0 || (SPECIES[s].features & FEATURE_SHELL) == 0);
        t.swim = 1.0f;
        if (!reads_weapon_or_armour(s)) {
            t.armour = t.weapon = 1.0f;
        }
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

/* shapes counts every shape a body asks for, also any past the list's end,
 * so this fails if a species in some pose wants more than the list holds. */
static void test_shape_list_fits(void)
{
    SpritePose poses[3];
    skeleton_pose_standing(&poses[0]);
    skeleton_pose_standing(&poses[1]);
    poses[1].jaw = 0.85f; /* an open mouth adds the mouth triangle */
    skeleton_pose_standing(&poses[2]);
    poses[2].action = ACTION_WALK;
    poses[2].phase = 2.0f;
    int most = 0;
    for (int s = 0; s < SPECIES_COUNT; s++) {
        for (int n = 0; n < 3; n++) {
            raster_species(&raster_a, s, largest_k(s), &poses[n], true, false);
            CHECK(raster_a.shapes > 10 && raster_a.shapes <= SKELETON_SHAPES_MAX);
            most = raster_a.shapes > most ? raster_a.shapes : most;
            raster_species(&raster_a, s, largest_k(s) * 1.6f, &poses[n], true, true);
            CHECK(raster_a.shapes > 10 && raster_a.shapes <= SKELETON_SHAPES_MAX);
        }
    }
    printf("  the most shapes a body asks for: %d of %d\n", most, SKELETON_SHAPES_MAX);
}

/* Poses will come from timing maths (task 11): a number that is not a number,
 * or a huge one, must neither crash nor ink outside the grid. */
static void test_wild_pose_is_safe(void)
{
    SpritePose p;
    skeleton_pose_standing(&p);
    p.phase = 1e30f;
    p.action = ACTION_WALK;
    raster_species(&raster_a, SPECIES_WOLF, largest_k(SPECIES_WOLF), &p, true, false);
    CHECK(raster_a.x1 <= raster_a.w && raster_a.y1 <= raster_a.h);
    p.head_dx = NAN;  /* NAN and INFINITY come from math.h */
    p.bob = INFINITY;
    p.jaw = NAN;
    raster_species(&raster_a, SPECIES_WOLF, largest_k(SPECIES_WOLF), &p, true, false);
    CHECK(raster_a.x1 <= raster_a.w && raster_a.y1 <= raster_a.h);
    CHECK(raster_a.eye_x >= -1 && raster_a.eye_x < raster_a.w);
    p.head_dx = 1e30f;
    p.bob = -1e30f;
    raster_species(&raster_a, SPECIES_ELEPHANT, largest_k(SPECIES_ELEPHANT), &p, true, true);
    CHECK(raster_a.x1 <= raster_a.w && raster_a.y1 <= raster_a.h);
    CHECK(raster_a.eye_x >= -1 && raster_a.eye_x < raster_a.w);
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
        /* The lowest ink is the outline under the feet, on the ground row,
         * for every species, pillar legs included. */
        CHECK(raster_a.y1 - 1 == raster_a.oy);
        CHECK(raster_b.y1 - 1 == raster_b.oy);
        if (raster_a.y1 - 1 != raster_a.oy || raster_b.y1 - 1 != raster_b.oy) {
            fprintf(stderr, "  %s: lowest rows %d, %d\n", SPECIES[s].name, raster_a.y1 - 1,
                    raster_b.y1 - 1);
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

/* ---- the species' own parts and marks (task 7) ---- */

static void raster_traits(SkeletonRaster *r, int species, const BodyTraits *t, float k,
                          bool marks)
{
    SpritePose p;
    Anatomy a;
    skeleton_pose_standing(&p);
    species_anatomy(&a, species, t, k);
    skeleton_raster(r, &a, &p, marks, false);
}

/* Pixels of this part whose ink is lo..hi; part -1 is any part. */
static int count_part_inks(const SkeletonRaster *r, int part, int lo, int hi)
{
    int n = 0;
    for (int i = 0; i < r->w * r->h; i++) {
        bool part_ok = part < 0 || r->part[i] == part;
        n += part_ok && r->ink[i] >= lo && r->ink[i] <= hi;
    }
    return n;
}

static int count_part(const SkeletonRaster *r, int part)
{
    int n = 0;
    for (int i = 0; i < r->w * r->h; i++) {
        n += r->part[i] == part;
    }
    return n;
}

/* The highest (smallest) row holding this part; h when there is none. */
static int top_row(const SkeletonRaster *r, int part)
{
    for (int i = 0; i < r->w * r->h; i++) {
        if (r->part[i] == part) {
            return i / r->w;
        }
    }
    return r->h;
}

/* The lowest row holding this part; -1 when there is none. */
static int bottom_row(const SkeletonRaster *r, int part)
{
    for (int i = r->w * r->h - 1; i >= 0; i--) {
        if (r->part[i] == part) {
            return i / r->w;
        }
    }
    return -1;
}

/* The longest run down one column of pixels of this part. */
static int longest_column_run(const SkeletonRaster *r, int part)
{
    int best = 0;
    for (int x = 0; x < r->w; x++) {
        int run = 0;
        for (int y = 0; y < r->h; y++) {
            run = r->part[y * r->w + x] == part ? run + 1 : 0;
            best = run > best ? run : best;
        }
    }
    return best;
}

/* PART_LEG pixels below the near knees by more than the knee's radius: the
 * shins and feet. Counts them, and how many are INK_DEEP. */
static void count_lower_legs(const SkeletonRaster *r, float knee_r, int *all, int *deep)
{
    float knee_y = fmaxf(r->knee[2][1], r->knee[3][1]);
    *all = *deep = 0;
    for (int y = (int)ceilf(knee_y + knee_r + 1.0f); y < r->h; y++) {
        for (int x = 0; x < r->w; x++) {
            int i = y * r->w + x;
            if (r->part[i] == PART_LEG) {
                (*all)++;
                *deep += r->ink[i] == INK_DEEP;
            }
        }
    }
}

static void test_each_species_shows_its_marks(void)
{
    SkeletonRaster *r = &raster_a;
    /* Stripes: zebra and tiger. */
    raster_standing(r, SPECIES_ZEBRA, largest_k(SPECIES_ZEBRA), false);
    CHECK(count_ink(r, INK_STRIPE) > 20);
    CHECK(count_part(r, PART_MANE) > 0); /* the neck mane */
    raster_standing(r, SPECIES_TIGER, largest_k(SPECIES_TIGER), false);
    CHECK(count_ink(r, INK_STRIPE) > 20);
    /* The lion's mane, in its own five shades. */
    raster_standing(r, SPECIES_LION, largest_k(SPECIES_LION), false);
    CHECK(count_part_inks(r, PART_MANE, INK_MANE0, INK_MANE0 + 4) > 30);
    /* The fox's dark stockings: every shin and foot pixel is the deepest
     * shade; the wolf's are not. */
    for (int i = 0; i < 2; i++) {
        int s = i == 0 ? SPECIES_FOX : SPECIES_WOLF;
        Anatomy a;
        BodyTraits t = role_traits(SPECIES[s].role);
        species_anatomy(&a, s, &t, largest_k(s));
        raster_standing(r, s, largest_k(s), false);
        int all, deep;
        count_lower_legs(r, a.leg_w * 0.45f, &all, &deep);
        CHECK(all > 4);
        CHECK(i == 0 ? deep == all : deep < all);
    }
    /* The lemur: a ringed tail and a dark mask, which the fennec lacks. */
    raster_standing(r, SPECIES_FENNEC, largest_k(SPECIES_FENNEC), false);
    int fennec_face = count_part_inks(r, PART_HEAD, INK_DEEP, INK_DEEP);
    raster_standing(r, SPECIES_LEMUR, largest_k(SPECIES_LEMUR), false);
    CHECK(count_part_inks(r, PART_TAIL, INK_STRIPE, INK_STRIPE) > 5);
    CHECK(count_part_inks(r, PART_HEAD, INK_DEEP, INK_DEEP) > fennec_face + 3);
    /* The elephant: a trunk and ivory tusks. */
    raster_standing(r, SPECIES_ELEPHANT, largest_k(SPECIES_ELEPHANT), false);
    CHECK(count_part(r, PART_TRUNK) > 10);
    CHECK(count_part_inks(r, PART_HORN, INK_IVORY0, INK_IVORY0 + 4) > 3);
    /* The rhino: horn above the nose, near it. */
    raster_standing(r, SPECIES_RHINO, largest_k(SPECIES_RHINO), false);
    int nose = -1;
    for (int i = 0; i < r->w * r->h && nose < 0; i++) {
        nose = r->ink[i] == INK_NOSE ? i : -1;
    }
    CHECK(nose >= 0);
    int horn_above = 0;
    for (int i = 0; i < r->w * r->h && nose >= 0; i++) {
        int dx = i % r->w - nose % r->w;
        horn_above += r->part[i] == PART_HORN && i / r->w < nose / r->w && dx > -8 && dx < 8 &&
                      r->ink[i] >= INK_HORN0 && r->ink[i] <= INK_HORN0 + 4;
    }
    CHECK(horn_above > 5);
    /* The armadillo's shell, banded. */
    raster_standing(r, SPECIES_ARMADILLO, largest_k(SPECIES_ARMADILLO), false);
    CHECK(count_part_inks(r, PART_SHELL, INK_SHELL1, INK_SHELL1 + 3) > 20);
    CHECK(count_part_inks(r, PART_SHELL, INK_SHELL_BAND, INK_SHELL_BAND) > 3);
    /* Antlers and lyre horns stand above the head. */
    const int horned[2] = {SPECIES_DEER, SPECIES_GAZELLE};
    for (int i = 0; i < 2; i++) {
        raster_standing(r, horned[i], largest_k(horned[i]), false);
        CHECK(count_part_inks(r, PART_HORN, INK_HORN0, INK_HORN0 + 4) > 5);
        CHECK(top_row(r, PART_HORN) < top_row(r, PART_HEAD));
    }
    /* The gazelle's dark side stripe and the goat's dark beard are thin
     * marks: looked for on the large grid, where they are a few pixels
     * wide. The side stripe: many more deep pixels on the body than the
     * deer has. */
    raster_standing(r, SPECIES_DEER, largest_k(SPECIES_DEER) * 1.6f, true);
    int deer_deep = count_part_inks(r, PART_BODY, INK_DEEP, INK_DEEP);
    raster_standing(r, SPECIES_GAZELLE, largest_k(SPECIES_GAZELLE) * 1.6f, true);
    int gazelle_deep = count_part_inks(r, PART_BODY, INK_DEEP, INK_DEEP);
    CHECK(gazelle_deep > deer_deep + 10);
    /* The goat: curled horns and a dark beard hanging below the head (its
     * lower side pale, as the jaw's underside rule makes it). */
    raster_standing(r, SPECIES_GOAT, largest_k(SPECIES_GOAT) * 1.6f, true);
    CHECK(count_part_inks(r, PART_HORN, INK_HORN0, INK_HORN0 + 4) > 10);
    int head_bottom = bottom_row(r, PART_HEAD), beard = 0;
    for (int i = (head_bottom + 1) * r->w; i < r->w * r->h; i++) {
        beard += r->part[i] == PART_JAW && r->ink[i] == INK_DEEP;
    }
    CHECK(beard >= 2);
    /* The beaver's paddle: where it shows behind the haunch, a flat tail
     * about 2 K thick, far thicker than a thin tail's root (0.6 K). */
    float k = largest_k(SPECIES_BEAVER);
    raster_standing(r, SPECIES_BEAVER, k, false);
    CHECK((float)longest_column_run(r, PART_TAIL) >= 1.8f * k);
}

/* The proportions of a table row, to compare two rows. */
static void proportions(const Species *sp, float out[12])
{
    const float v[12] = {sp->scale, sp->body_len, sp->chest, sp->hip, sp->leg_len, sp->leg_w,
                         sp->neck_len, sp->neck_angle, sp->skull, sp->muzzle, sp->muzzle_r,
                         sp->ear_size};
    memcpy(out, v, sizeof v);
}

/* The design's anti-goal: two species of a role must not be two colours of
 * one animal. */
static void test_species_of_a_role_differ_in_shape(void)
{
    for (int s1 = 0; s1 < SPECIES_COUNT; s1++) {
        for (int s2 = s1 + 1; s2 < SPECIES_COUNT; s2++) {
            const Species *a = &SPECIES[s1], *b = &SPECIES[s2];
            if (a->role != b->role) {
                continue;
            }
            float pa[12], pb[12];
            proportions(a, pa);
            proportions(b, pb);
            bool differ = a->ear != b->ear || a->tail != b->tail || a->features != b->features;
            for (int i = 0; i < 12; i++) {
                differ = differ || fabsf(pa[i] - pb[i]) >= 0.1f;
            }
            CHECK(differ);
            /* Rastered standing at the same K, with the role's genes. */
            BodyTraits t = role_traits(a->role);
            raster_traits(&raster_a, s1, &t, 1.2f, true);
            raster_traits(&raster_b, s2, &t, 1.2f, true);
            int filled_px = 0, different = 0;
            for (int i = 0; i < raster_a.w * raster_a.h; i++) {
                bool any = raster_a.part[i] != PART_NONE || raster_b.part[i] != PART_NONE;
                filled_px += any;
                different += any && raster_a.part[i] != raster_b.part[i];
            }
            CHECK(different * 20 >= filled_px);
            if (different * 20 < filled_px) {
                fprintf(stderr, "  %s and %s: %d of %d differ\n", a->name, b->name, different,
                        filled_px);
            }
        }
    }
}

static void test_weapon_lengthens_horns(void)
{
    const int horned[5] = {SPECIES_DEER, SPECIES_GOAT, SPECIES_GAZELLE, SPECIES_RHINO,
                           SPECIES_ELEPHANT};
    for (int i = 0; i < 5; i++) {
        int s = horned[i];
        BodyTraits t = role_traits(SPECIES[s].role);
        t.weapon = 0.0f;
        raster_traits(&raster_a, s, &t, 1.2f, true);
        t.weapon = 1.0f;
        raster_traits(&raster_b, s, &t, 1.2f, true);
        int short_horns = count_part(&raster_a, PART_HORN);
        int long_horns = count_part(&raster_b, PART_HORN);
        CHECK(short_horns > 0);
        CHECK(long_horns > short_horns);
        if (s != SPECIES_ELEPHANT) { /* tusks point forward, horns up */
            CHECK(top_row(&raster_b, PART_HORN) < top_row(&raster_a, PART_HORN));
        }
    }
}

static void test_armour_grows_the_shell(void)
{
    BodyTraits t = role_traits(ROLE_ARMOURED);
    t.armour = 0.0f;
    raster_traits(&raster_a, SPECIES_ARMADILLO, &t, 1.2f, true);
    t.armour = 1.0f;
    raster_traits(&raster_b, SPECIES_ARMADILLO, &t, 1.2f, true);
    CHECK(count_part(&raster_a, PART_SHELL) > 0);
    CHECK(count_part(&raster_b, PART_SHELL) > count_part(&raster_a, PART_SHELL));
    CHECK(top_row(&raster_b, PART_SHELL) <= top_row(&raster_a, PART_SHELL));
}

/* The far zoom's silhouette: no marks, but horns, tusks, trunk, mane and
 * shell stay, as parts of the outline. */
static void test_far_silhouette_keeps_parts_drops_marks(void)
{
    const int species[6] = {SPECIES_ZEBRA, SPECIES_TIGER, SPECIES_ARMADILLO, SPECIES_DEER,
                            SPECIES_ELEPHANT, SPECIES_LION};
    for (int i = 0; i < 6; i++) {
        int s = species[i];
        BodyTraits t = role_traits(SPECIES[s].role);
        raster_traits(&raster_a, s, &t, largest_k(s), true);
        raster_traits(&raster_b, s, &t, largest_k(s), false);
        CHECK(count_part_inks(&raster_b, PART_BODY, INK_STRIPE, INK_STRIPE) == 0);
        CHECK(count_ink(&raster_b, INK_SHELL_BAND) == 0);
        CHECK(count_part(&raster_b, PART_HORN) == count_part(&raster_a, PART_HORN));
        CHECK(count_part(&raster_b, PART_TRUNK) == count_part(&raster_a, PART_TRUNK));
        CHECK(count_part(&raster_b, PART_SHELL) == count_part(&raster_a, PART_SHELL));
        /* Untrimmed, the mane is a whole oval: never smaller. */
        CHECK(count_part(&raster_b, PART_MANE) >= count_part(&raster_a, PART_MANE));
    }
    BodyTraits bare = {0}; /* every trait 0: the smallest shell */
    raster_traits(&raster_b, SPECIES_ARMADILLO, &bare, 1.2f, false);
    CHECK(count_part_inks(&raster_b, PART_SHELL, INK_SHELL1, INK_SHELL1 + 3) > 0);
}

int main(void)
{
    test_same_input_same_pixels();
    test_everything_inside_the_grid();
    test_other_traits_change_nothing();
    test_planted_leg_is_straight();
    test_lifted_leg_bends();
    test_shape_list_fits();
    test_wild_pose_is_safe();
    test_large_grid_keeps_the_ground_point();
    test_crouch_bends_the_knees();
    test_no_lone_shade_pixel();
    test_outline_touches_body_and_softens_on_top();
    test_jaw_opens();
    test_far_silhouette_has_no_marks();
    test_each_species_shows_its_marks();
    test_species_of_a_role_differ_in_shape();
    test_weapon_lengthens_horns();
    test_armour_grows_the_shell();
    test_far_silhouette_keeps_parts_drops_marks();
    TEST_SUMMARY();
}
