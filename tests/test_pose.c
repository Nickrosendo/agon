#include "test.h"

#include <math.h>   /* fabsf, fmaxf, sqrtf, NAN */
#include <stdlib.h> /* abs */

#include "pose.h"
#include "skeleton.h"
#include "species.h"
#include "sprite.h"

/* About 22 KB each: static, so they live outside the stack. */
static SkeletonRaster standing, sleeping;

/* A middling body of this species, at Normal. */
static SpriteBody middling(int species)
{
    BodyTraits t = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
    return sprite_body_of(&t, species, 1.0f, SPRITE_BASE_NORMAL);
}

/* The lowest grid row with body on it, or -1 when there is none. Rows count
 * down from the top, so a larger row is nearer the ground. */
static int lowest_body_row(const SkeletonRaster *r)
{
    for (int y = r->h - 1; y >= 0; y--) {
        for (int x = 0; x < r->w; x++) {
            if (r->part[y * r->w + x] == PART_BODY) {
                return y;
            }
        }
    }
    return -1;
}

/* How far the knee is from the hip, in pixels. */
static float hip_to_knee(const SkeletonRaster *r, int leg)
{
    float dx = r->knee[leg][0] - r->hip[leg][0], dy = r->knee[leg][1] - r->hip[leg][1];
    return sqrtf(dx * dx + dy * dy);
}

static void raster_of(SkeletonRaster *out, const SpriteBody *b, int action, int frame)
{
    Anatomy a;
    species_anatomy(&a, b->species, &b->traits, sprite_k(b));
    SpritePose p;
    pose_at(&p, b, action, frame);
    skeleton_raster(out, &a, &p, true, false);
}

/* Asleep, every species lies down: its body drops to the ground. */
static void test_sleep_folds_the_legs(void)
{
    for (int s = 0; s < SPECIES_COUNT; s++) {
        SpriteBody b = middling(s);
        raster_of(&standing, &b, ACTION_REST, 0);
        raster_of(&sleeping, &b, ACTION_SLEEP, 0);
        int stand_row = lowest_body_row(&standing);
        int sleep_row = lowest_body_row(&sleeping);
        CHECK(stand_row >= 0 && sleep_row >= 0);
        CHECK(sleep_row > stand_row);              /* lower than standing */
        CHECK(sleeping.oy - sleep_row <= 2);       /* and on the ground */
        /* Every knee folded up towards its hip. Not "higher on the grid":
         * the whole body drops to the ground, so the knees end lower there. */
        for (int leg = 0; leg < 4; leg++) {
            CHECK(hip_to_knee(&sleeping, leg) < hip_to_knee(&standing, leg));
        }
        if (sleep_row <= stand_row || sleeping.oy - sleep_row > 2) {
            printf("  species %d: standing row %d, asleep row %d, ground %d\n", s,
                   stand_row, sleep_row, sleeping.oy);
        }
    }
}

/* Asleep, the eyes stay shut in every frame. */
static void test_sleep_shuts_the_eyes(void)
{
    SpriteBody b = middling(SPECIES_DEER);
    for (int f = 0; f < SPRITE_FRAMES[ACTION_SLEEP]; f++) {
        SpritePose p;
        pose_at(&p, &b, ACTION_SLEEP, f);
        CHECK(p.eyes == 0.0f);
        CHECK(p.fold == 1.0f);
        CHECK(p.action == ACTION_SLEEP);
    }
}

/* The breath rises and falls within the cycle, and the last frame leads
 * smoothly back into the first, so the loop has no jump. */
static void test_sleep_breath_loops(void)
{
    SpriteBody b = middling(SPECIES_DEER);
    int n = SPRITE_FRAMES[ACTION_SLEEP];
    float low = 1e9f, high = -1e9f;
    for (int f = 0; f < n; f++) {
        SpritePose now, next;
        pose_at(&now, &b, ACTION_SLEEP, f);
        pose_at(&next, &b, ACTION_SLEEP, (f + 1) % n);
        low = fminf(low, now.breath);
        high = fmaxf(high, now.breath);
        /* A sine of depth 0.45 over n steps moves at most 0.45 * sin(pi / n)
         * between neighbours: half its swing for 6 frames. A jump where the
         * loop starts again would be larger. */
        CHECK(fabsf(next.breath - now.breath) <= 0.45f * 0.5f + 1e-4f);
    }
    CHECK(high - low > 0.2f);
}

/* The frame at a body time: the cycle runs through all its frames in its
 * seconds, and two ids side by side are out of step. */
static void test_frame_follows_the_clock(void)
{
    int n = SPRITE_FRAMES[ACTION_SLEEP];
    uint64_t step_ms = (uint64_t)(POSE_SLEEP_SECONDS * 1000.0f) / (uint64_t)n;
    float sec = POSE_SLEEP_SECONDS;
    int first = pose_frame(0, 7, ACTION_SLEEP, sec);
    CHECK(first >= 0 && first < n);
    CHECK(pose_frame(step_ms, 7, ACTION_SLEEP, sec) == (first + 1) % n);
    /* A whole cycle later, the same frame. */
    CHECK(pose_frame((uint64_t)(POSE_SLEEP_SECONDS * 1000.0f), 7, ACTION_SLEEP, sec) == first);
    CHECK(pose_frame(0, 8, ACTION_SLEEP, sec) != first);
    /* An action out of range, or a cycle with no length, is always frame 0. */
    CHECK(pose_frame(123456, 7, -1, sec) == 0);
    CHECK(pose_frame(123456, 7, ACTION_SLEEP, 0.0f) == 0);
    /* Twice the pace, twice the frames in the same time. */
    CHECK(pose_frame(step_ms, 7, ACTION_SLEEP, sec / 2.0f) == (first + 2) % n);
}

/* The cache keeps the sleeping picture apart from the resting one. */
static void test_sleeping_picture_differs(void)
{
    SpriteCache c;
    CHECK(sprite_cache_init(&c, 64, 16));
    SpriteBody b = middling(SPECIES_DEER);
    SpritePicture rest = sprite_picture(&c, &b, ACTION_REST, 0);
    int rest_h = rest.y1 - rest.y0;
    SpritePicture sleep = sprite_picture(&c, &b, ACTION_SLEEP, 0);
    int sleep_h = sleep.y1 - sleep.y0;
    CHECK(sleep_h < rest_h); /* lying down, it is lower */
    sprite_cache_free(&c);
}


/* ---- Every cycle ---- */

/* The actions that have a cycle of their own, the attack aside. */
static const int CYCLES[] = {ACTION_REST, ACTION_WALK, ACTION_RUN, ACTION_GRAZE,
                             ACTION_BITE, ACTION_SLEEP};
#define CYCLE_COUNT ((int)(sizeof(CYCLES) / sizeof(CYCLES[0])))

/* The largest change between two poses of the values that move the body as a
 * whole, in K units: one pixel at K 1. */
static float largest_change(const SpritePose *a, const SpritePose *b)
{
    const float pairs[][2] = {
        {a->body_dx, b->body_dx}, {a->bob, b->bob},         {a->crouch, b->crouch},
        {a->head_dx, b->head_dx}, {a->head_dy, b->head_dy}, {a->stretch, b->stretch},
        {a->breath, b->breath},
    };
    float most = 0.0f;
    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++) {
        most = fmaxf(most, fabsf(pairs[i][1] - pairs[i][0]));
    }
    return most;
}

/* How far a pose value may move between two frames: 1, or 2 where the mockup
 * itself moves faster, the bite's lunge and a hare's hop. The lunge here also
 * covers the frame that ends it: the change is counted against the frame it
 * leads into. */
static float allowed_change(int species, int action, float u_next)
{
    if (action == ACTION_BITE && u_next >= 0.28f && u_next <= 0.47f) {
        /* The mockup's ease-back lunge, cut into 39 frames, moves the head 2.17
         * between frames 11 and 12: its own speed, a little over 2. */
        return 2.2f;
    }
    if (SPECIES[species].hop && (action == ACTION_WALK || action == ACTION_RUN)) {
        return 2.0f;
    }
    return 1.0f;
}

static SkeletonRaster before, after;

/* Every species, every cycle, every pair of neighbouring frames and the last
 * back to the first: the body moves at most a pixel at K 1, and so do walking
 * feet (in K units too, so a large elephant's are not held to a small hare's
 * pixels). */
static void test_poses_are_continuous(void)
{
    for (int sp = 0; sp < SPECIES_COUNT; sp++) {
        SpriteBody b = middling(sp);
        float k_units = sprite_k(&b);
        for (int k = 0; k < CYCLE_COUNT; k++) {
            int action = CYCLES[k], n = SPRITE_FRAMES[action];
            for (int f = 0; f < n; f++) {
                SpritePose now, next;
                pose_at(&now, &b, action, f);
                pose_at(&next, &b, action, (f + 1) % n);
                float u_next = (float)((f + 1) % n) / (float)n;
                float change = largest_change(&now, &next);
                float allowed = allowed_change(sp, action, u_next);
                CHECK(change <= allowed + 1e-4f);
                if (change > allowed + 1e-4f) {
                    printf("  %s action %d frame %d: %.3f\n", SPECIES[sp].name, action,
                           f, (double)change);
                }
                if (action != ACTION_WALK || SPECIES[sp].hop) {
                    continue;
                }
                raster_of(&before, &b, action, f);
                raster_of(&after, &b, action, (f + 1) % n);
                for (int leg = 0; leg < 4; leg++) {
                    float dx = (after.foot[leg][0] - before.foot[leg][0]) / k_units;
                    float dy = (after.foot[leg][1] - before.foot[leg][1]) / k_units;
                    bool small = fabsf(dx) <= 1.0f && fabsf(dy) <= 1.0f;
                    CHECK(small);
                    if (!small) {
                        printf("  %s walk frame %d leg %d: %.2f, %.2f\n", SPECIES[sp].name,
                               f, leg, (double)dx, (double)dy);
                    }
                }
            }
        }
    }
}

/* How far the knee is from the straight line from hip to foot, in pixels. */
static float knee_off_line(const SkeletonRaster *r, int leg)
{
    float hx = r->hip[leg][0], hy = r->hip[leg][1];
    float fx = r->foot[leg][0], fy = r->foot[leg][1];
    float kx = r->knee[leg][0], ky = r->knee[leg][1];
    float vx = fx - hx, vy = fy - hy;
    float len = sqrtf(vx * vx + vy * vy);
    if (len < 1e-4f) {
        return 0.0f;
    }
    /* The cross product of hip-to-foot and hip-to-knee, over the length. */
    return fabsf(vx * (ky - hy) - vy * (kx - hx)) / len;
}

/* In every walk frame some foot is planted with its leg straight; in some
 * frame a foot is lifted more than a pixel, its knee bent by more than a pixel
 * (pillar legs, the elephant's kind, stay straight by design). Hopping species
 * bound on all fours instead. On the detailed pictures: at Normal a short-legged
 * otter lifts its feet under a pixel, which no knee can show. */
static void test_walk_has_a_planted_and_a_lifted_foot(void)
{
    /* Detailed pictures need the large grid: raster_of draws the small one. */
    for (int sp = 0; sp < SPECIES_COUNT; sp++) {
        if (SPECIES[sp].hop) {
            continue;
        }
        BodyTraits t = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};
        SpriteBody b = sprite_body_of(&t, sp, 1.0f, SPRITE_BASE_DETAIL);
        bool lifted_bent = false;
        for (int f = 0; f < SPRITE_FRAMES[ACTION_WALK]; f++) {
            Anatomy a;
            species_anatomy(&a, b.species, &b.traits, sprite_k(&b));
            SpritePose p;
            pose_at(&p, &b, ACTION_WALK, f);
            skeleton_raster(&before, &a, &p, true, true);
            bool planted_straight = false;
            for (int leg = 0; leg < 4; leg++) {
                bool on_ground = before.foot[leg][1] >= (float)before.oy - 0.01f;
                float off = knee_off_line(&before, leg);
                if (on_ground && off <= 1.0f) {
                    planted_straight = true;
                }
                bool bent = off > 1.0f || SPECIES[sp].pillar;
                if (before.foot[leg][1] < (float)before.oy - 1.0f && bent) {
                    lifted_bent = true;
                }
            }
            CHECK(planted_straight);
            if (!planted_straight) {
                printf("  %s walk frame %d: no planted straight leg\n", SPECIES[sp].name, f);
            }
        }
        CHECK(lifted_bent);
        if (!lifted_bent) {
            printf("  %s: no lifted bent leg\n", SPECIES[sp].name);
        }
    }
}

/* The lowest grid row with any ink, or -1. */
static int lowest_ink_row(const SkeletonRaster *r)
{
    return r->y1 > r->y0 ? r->y1 - 1 : -1;
}

/* A hare's body leaves the ground in some walk frame; a deer's never rises
 * more than a pixel. */
static void test_hare_hops(void)
{
    SpriteBody hare = middling(SPECIES_HARE), deer = middling(SPECIES_DEER);
    raster_of(&standing, &hare, ACTION_REST, 0);
    int hare_ground = lowest_ink_row(&standing);
    raster_of(&standing, &deer, ACTION_REST, 0);
    int deer_ground = lowest_ink_row(&standing);
    int hare_most = 0, deer_most = 0;
    for (int f = 0; f < SPRITE_FRAMES[ACTION_WALK]; f++) {
        raster_of(&before, &hare, ACTION_WALK, f);
        int rise = hare_ground - lowest_ink_row(&before);
        hare_most = rise > hare_most ? rise : hare_most;
        raster_of(&before, &deer, ACTION_WALK, f);
        rise = deer_ground - lowest_ink_row(&before);
        deer_most = rise > deer_most ? rise : deer_most;
    }
    CHECK(hare_most >= 1);
    CHECK(deer_most <= 1);
}

/* Grazing, the head goes down to the grass and comes back up by the end. */
static void test_graze_lowers_the_head(void)
{
    SpriteBody b = middling(SPECIES_DEER);
    raster_of(&standing, &b, ACTION_REST, 0);
    int n = SPRITE_FRAMES[ACTION_GRAZE];
    raster_of(&before, &b, ACTION_GRAZE, n / 2);   /* nibbling */
    raster_of(&after, &b, ACTION_GRAZE, 0);        /* the cycle's start: up */
    CHECK(standing.eye_y >= 0 && before.eye_y >= 0);
    CHECK(before.eye_y >= standing.eye_y + 3);
    CHECK(abs(after.eye_y - standing.eye_y) <= 1);
    /* The jaw works while the head is down. */
    float most = 0.0f;
    for (int f = 0; f < n; f++) {
        SpritePose p;
        pose_at(&p, &b, ACTION_GRAZE, f);
        most = fmaxf(most, p.jaw);
    }
    CHECK(most > 0.1f);
}

/* Over one blink cycle the eyes go 1, 0.4, 0, 0.4 and back to 1, 60 ms each
 * step, whatever the id. */
static void test_blink_in_two_steps(void)
{
    for (uint32_t id = 0; id < 5; id++) {
        int at_half = 0, at_shut = 0, changes = 0;
        bool order = true;
        float last = pose_eyes(3599, id);
        for (uint64_t ms = 0; ms < 3600; ms++) {
            float e = pose_eyes(ms, id);
            at_half += e == 0.4f;
            at_shut += e == 0.0f;
            if (e != last) {
                changes++;
                /* From open or shut, always to half; from half, to either. */
                order = order && (last == 0.4f || e == 0.4f);
            }
            last = e;
        }
        CHECK(at_half == 120 && at_shut == 60);
        CHECK(changes == 4 && order);
        /* The same time a cycle later. */
        CHECK(pose_eyes(1000, id) == pose_eyes(4600, id));
    }
    /* Two ids blink at different times. */
    uint64_t first[2] = {0, 0};
    for (uint32_t id = 1; id <= 2; id++) {
        while (pose_eyes(first[id - 1], id) == 1.0f) {
            first[id - 1]++;
        }
    }
    CHECK(first[0] != first[1]);
}

/* The walk speeds up to keep up with a quick glide, never above
 * POSE_CADENCE_MAX, and never slower than the body's own pace. */
static void test_cadence_keeps_up(void)
{
    SpriteBody b = middling(SPECIES_DEER);
    float slow = pose_cadence(&b, ACTION_WALK, 0.1f);
    float quick = pose_cadence(&b, ACTION_WALK, 10.0f);
    CHECK(slow == 1.0f);
    CHECK(quick > 1.0f && quick <= POSE_CADENCE_MAX);
    CHECK(pose_cadence(&b, ACTION_WALK, 1000.0f) == POSE_CADENCE_MAX);
    CHECK(pose_cadence(&b, ACTION_REST, 10.0f) == 1.0f);
    CHECK(pose_cadence(&b, ACTION_WALK, NAN) == 1.0f);
    /* The same at every zoom. */
    SpriteBody far = b;
    far.base = SPRITE_BASE_FAR;
    CHECK(pose_cadence(&far, ACTION_WALK, 10.0f) == quick);
    /* A run's cycle is shorter than a walk's. */
    CHECK(pose_cycle_seconds(&b, ACTION_RUN) < pose_cycle_seconds(&b, ACTION_WALK));
}

/* ---- The attack ---- */

/* The attack's pose at share c of the way through it (0..1). */
static void attack_at(SpritePose *p, const SpriteBody *b, float c)
{
    int n = SPRITE_FRAMES[ACTION_ATTACK];
    pose_at(p, b, ACTION_ATTACK, (int)(c * (float)n));
}

/* The attack's five parts come in order: the crouch deepens through the
 * stalk, the body flies forward in the pounce and is furthest out after it,
 * the jaws open in the flight and are shut by the end of the impact, and only
 * the cats swipe with a paw. */
static void test_attack_parts_in_order(void)
{
    for (int sp = 0; sp < SPECIES_COUNT; sp++) {
        SpriteBody b = middling(sp);
        SpritePose early, late_stalk, mid_pounce, impact_end, hold, release_end;
        attack_at(&early, &b, 0.05f);
        attack_at(&late_stalk, &b, 0.33f);
        attack_at(&mid_pounce, &b, 0.45f);
        attack_at(&impact_end, &b, 0.57f);
        attack_at(&hold, &b, 0.62f);
        attack_at(&release_end, &b, 0.99f);
        CHECK(early.action == ACTION_ATTACK);
        CHECK(late_stalk.crouch > early.crouch);
        CHECK(late_stalk.crouch > 1.0f);          /* low before the leap */
        CHECK(mid_pounce.jaw > 0.7f);             /* open in the flight */
        CHECK(impact_end.jaw < 0.1f);             /* shut on the neck */
        CHECK(impact_end.body_dx > 4.0f);         /* furthest out */
        CHECK(impact_end.body_dx > mid_pounce.body_dx);
        CHECK(release_end.body_dx < 0.5f);        /* back where it started */
        bool feline = sp == SPECIES_LION || sp == SPECIES_TIGER;
        CHECK(feline ? hold.swipe > 0.3f : hold.swipe == 0.0f);
        /* Only the release steps; it alone moves the feet. */
        CHECK(release_end.stepping && !hold.stepping);
    }
    CHECK(pose_cycle_seconds(NULL, ACTION_ATTACK) == POSE_ATTACK_SECONDS);
}

/* How far an attack's pose value may move between two frames: 2 in the
 * pounce (the mockup's own pounce moves 1.6 a frame at K 1), 1 elsewhere,
 * the impact included. */
static float attack_allowed(float c_next)
{
    return c_next > 0.35f && c_next <= 0.5f + 1e-4f ? 2.0f : 1.0f;
}

/* Frame by frame through the whole attack, and from the end of the hold back
 * to its start (a long fight repeats the hold): no jump larger than allowed. */
static void test_attack_is_continuous(void)
{
    int n = SPRITE_FRAMES[ACTION_ATTACK];
    int hold_first = (int)(0.56f * (float)n + 0.999f);
    int hold_last = (int)(0.8f * (float)n) - 1;
    for (int sp = 0; sp < SPECIES_COUNT; sp++) {
        SpriteBody b = middling(sp);
        for (int f = 0; f + 1 < n; f++) {
            SpritePose now, next;
            pose_at(&now, &b, ACTION_ATTACK, f);
            pose_at(&next, &b, ACTION_ATTACK, f + 1);
            float c_next = (float)(f + 1) / (float)n;
            float change = largest_change(&now, &next);
            CHECK(change <= attack_allowed(c_next) + 1e-4f);
            if (change > attack_allowed(c_next) + 1e-4f) {
                printf("  %s attack frame %d: %.3f\n", SPECIES[sp].name, f, (double)change);
            }
        }
        SpritePose end, start;
        pose_at(&end, &b, ACTION_ATTACK, hold_last);
        pose_at(&start, &b, ACTION_ATTACK, hold_first);
        CHECK(largest_change(&end, &start) <= 1.0f + 1e-4f);
    }
}

/* The victim's side: a white flash only at the bite (c 0.5 to 0.54), a push
 * away that starts at the bite and is gone again by the release (c 0.8), and
 * a tremble only while held. */
static void test_hit_flash_and_knock(void)
{
    float flash, knock, tremble;
    for (int i = 0; i <= 100; i++) {
        float c = (float)i / 100.0f;
        pose_hit(c, &flash, &knock, &tremble);
        bool in_flash = c >= 0.5f && c < 0.54f;
        CHECK(in_flash ? flash > 0.5f : flash == 0.0f);
        if (c < 0.5f || c >= 0.8f) {
            CHECK(knock == 0.0f);
        }
        CHECK(knock >= 0.0f && knock <= 2.5f);
        CHECK((c >= 0.56f && c < 0.8f) ? tremble > 0.0f : tremble == 0.0f);
    }
    pose_hit(0.56f, &flash, &knock, &tremble);
    CHECK(knock > 1.0f); /* pushed well back just after the bite */
    pose_hit(0.7999f, &flash, &knock, &tremble);
    CHECK(knock < 0.05f); /* and back by the release: no jump at 0.8 */
}

int main(void)
{
    test_sleep_folds_the_legs();
    test_sleep_shuts_the_eyes();
    test_sleep_breath_loops();
    test_attack_parts_in_order();
    test_attack_is_continuous();
    test_hit_flash_and_knock();
    test_frame_follows_the_clock();
    test_sleeping_picture_differs();
    test_poses_are_continuous();
    test_walk_has_a_planted_and_a_lifted_foot();
    test_hare_hops();
    test_graze_lowers_the_head();
    test_blink_in_two_steps();
    test_cadence_keeps_up();
    TEST_SUMMARY();
}
