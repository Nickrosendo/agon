#include "test.h"

#include <math.h> /* fabsf */

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

/* Until the rest of the animation stage, every other action stands. */
static void test_other_actions_stand(void)
{
    SpriteBody b = middling(SPECIES_WOLF);
    SpritePose want;
    skeleton_pose_standing(&want);
    for (int act = 0; act < ACTION_COUNT; act++) {
        if (act == ACTION_SLEEP) {
            continue;
        }
        SpritePose p;
        pose_at(&p, &b, act, 0);
        CHECK(p.fold == 0.0f && p.eyes == want.eyes && p.breath == 0.0f);
        CHECK(p.stride == want.stride && p.lift == want.lift);
    }
}

/* The frame at a body time: the cycle runs through all its frames in its
 * seconds, and two ids side by side are out of step. */
static void test_frame_follows_the_clock(void)
{
    int n = SPRITE_FRAMES[ACTION_SLEEP];
    uint64_t step_ms = (uint64_t)(POSE_SLEEP_SECONDS * 1000.0f) / (uint64_t)n;
    int first = pose_frame(0, 7, ACTION_SLEEP);
    CHECK(first >= 0 && first < n);
    CHECK(pose_frame(step_ms, 7, ACTION_SLEEP) == (first + 1) % n);
    /* A whole cycle later, the same frame. */
    CHECK(pose_frame((uint64_t)(POSE_SLEEP_SECONDS * 1000.0f), 7, ACTION_SLEEP) == first);
    CHECK(pose_frame(0, 8, ACTION_SLEEP) != first);
    /* A cycle with one frame, or an action out of range, is always frame 0. */
    CHECK(pose_frame(123456, 7, -1) == 0);
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

int main(void)
{
    test_sleep_folds_the_legs();
    test_sleep_shuts_the_eyes();
    test_sleep_breath_loops();
    test_other_actions_stand();
    test_frame_follows_the_clock();
    test_sleeping_picture_differs();
    TEST_SUMMARY();
}
