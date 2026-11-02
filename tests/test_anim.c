#include "test.h"

#include <math.h> /* fabsf */

#include "anim.h"
#include "settings.h"
#include "sim.h"
#include "sprite.h"

#define NEAR(a, b) (fabsf((a) - (b)) < 0.001f)

/* A real Sim with its creatures removed, so a test can place its own. */
static void empty_sim(Sim *s, int w, int h)
{
    Settings set = settings_default();
    CHECK(sim_init(s, &set, 1, w, h));
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            pool_kill(&s->pool, &s->world, i, &s->set);
        }
    }
    /* Every cell free soil, so any position is allowed. */
    for (int i = 0; i < w * h; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
    }
}

/* Moves a creature as the simulation would: occupant layer included. */
static void move_to(Sim *s, int slot, int x, int y)
{
    Creature *c = &s->pool.items[slot];
    s->world.occupant[world_idx(&s->world, c->x, c->y)] = -1;
    c->x = x;
    c->y = y;
    s->world.occupant[world_idx(&s->world, x, y)] = slot;
}

/* Sets the speed gene so one move takes exactly move_ticks ticks. */
static void set_move_ticks(Sim *s, int slot, float move_ticks)
{
    float speed = 1.0f / move_ticks;
    s->pool.items[slot].genome.g[GENE_SPEED] =
        (speed - s->set.speed_min) / (1.0f - s->set.speed_min);
}

static void test_glide_halfway(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    set_move_ticks(&s, slot, 4.0f);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    s.tick++;
    move_to(&s, slot, 11, 10);
    anim_update(&a, &s, 10);
    s.tick += 1;
    anim_update(&a, &s, 10);
    s.tick += 1;
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 10.5f));
    CHECK(NEAR(p.y, 10.0f));
    CHECK(p.frame >= FRAME_WALK0 && p.frame <= FRAME_WALK3);
    /* The fraction of a tick counts too: smooth between ticks. */
    anim_pose(&a, &s, slot, 0.5f, 0, &p);
    CHECK(NEAR(p.x, 10.625f));
    /* When the move is over: on the cell, and resting. */
    s.tick += 2;
    anim_update(&a, &s, 10);
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 11.0f));
    CHECK(p.frame == FRAME_REST0 || p.frame == FRAME_REST1);
    anim_free(&a);
    sim_free(&s);
}

static void test_glide_across_wrap(void)
{
    Sim s;
    empty_sim(&s, 256, 16);
    int slot = pool_spawn(&s.pool, &s.world, 255, 5);
    set_move_ticks(&s, slot, 2.0f);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    s.tick++;
    move_to(&s, slot, 0, 5);
    anim_update(&a, &s, 10);
    s.tick++;
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 255.5f)); /* never about 127 */
    anim_free(&a);
    sim_free(&s);
}

static void test_jump_above_15_ticks(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    set_move_ticks(&s, slot, 4.0f);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 30);
    s.tick++;
    move_to(&s, slot, 11, 10);
    anim_update(&a, &s, 30);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.5f, 0, &p);
    CHECK(NEAR(p.x, 11.0f));
    CHECK(p.frame >= FRAME_WALK0 && p.frame <= FRAME_WALK3); /* legs still */
    anim_free(&a);
    sim_free(&s);
}

static void test_jump_when_several_ticks_passed(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    set_move_ticks(&s, slot, 4.0f);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    s.tick += 3;
    move_to(&s, slot, 13, 10);
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 13.0f));
    anim_free(&a);
    sim_free(&s);
}

static void test_first_sight_does_not_glide(void)
{
    /* As after --load: a large tick, a creature already mid-world. */
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 40, 20);
    s.pool.items[slot].age = 5000;
    s.tick = 1000000;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 40.0f));
    CHECK(NEAR(p.y, 20.0f));
    CHECK(p.frame == FRAME_REST0 || p.frame == FRAME_REST1);
    CHECK(NEAR(p.pop, 1.0f)); /* old: no birth to show */
    anim_free(&a);
    sim_free(&s);
}

static void test_new_id_in_slot_forgets_old(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    s.pool.items[slot].age = 500;
    s.pool.items[slot].heading = HEADING_WEST;
    s.tick = 1000;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.flip);

    pool_kill(&s.pool, &s.world, slot, &s.set);
    s.world.carrion[world_idx(&s.world, 10, 10)] = 0.0f;
    int again = pool_spawn(&s.pool, &s.world, 50, 25);
    CHECK(again == slot); /* the free list hands the same slot back */
    s.tick++;
    anim_update(&a, &s, 10);
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 50.0f));
    CHECK(NEAR(p.y, 25.0f));
    CHECK(p.pop < 1.0f);
    CHECK(!p.flip);
    anim_free(&a);
    sim_free(&s);
}

static void test_reset_after_ids_restart(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    uint32_t id = s.pool.items[slot].id;
    set_move_ticks(&s, slot, 4.0f);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    /* The same slot and id, somewhere else: what r can produce. */
    anim_reset(&a);
    move_to(&s, slot, 30, 20);
    CHECK(s.pool.items[slot].id == id);
    s.tick++;
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 30.0f));
    CHECK(NEAR(p.y, 20.0f));
    anim_free(&a);
    sim_free(&s);
}

static void test_facing_north_keeps_last(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    AnimPose p;
    s.pool.items[slot].heading = HEADING_EAST;
    anim_update(&a, &s, 10);
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(!p.flip);
    s.pool.items[slot].heading = HEADING_NORTH;
    anim_update(&a, &s, 10);
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(!p.flip);
    s.pool.items[slot].heading = HEADING_WEST;
    anim_update(&a, &s, 10);
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.flip);
    s.pool.items[slot].heading = HEADING_SOUTH;
    anim_update(&a, &s, 10);
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.flip);
    anim_free(&a);
    sim_free(&s);
}

static void test_rest_breathing_offset(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int one = pool_spawn(&s.pool, &s.world, 10, 10);
    int two = pool_spawn(&s.pool, &s.world, 20, 10);
    /* Consecutive ids, so their remainders by 2 differ. */
    CHECK(s.pool.items[two].id == s.pool.items[one].id + 1);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p1, p2;
    anim_pose(&a, &s, one, 0.0f, 0, &p1);
    anim_pose(&a, &s, two, 0.0f, 0, &p2);
    CHECK(p1.frame != p2.frame);
    /* A breath later, each has changed. */
    AnimPose later;
    anim_pose(&a, &s, one, 0.0f, 625, &later);
    CHECK(later.frame != p1.frame);
    anim_free(&a);
    sim_free(&s);
}

static void test_bite_frame(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    s.pool.items[slot].current.kind = ACT_BITE;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.frame == FRAME_BITE);
    s.pool.items[slot].current.kind = ACT_SPLIT;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.frame == FRAME_SPLIT);
    anim_free(&a);
    sim_free(&s);
}

int main(void)
{
    test_glide_halfway();
    test_glide_across_wrap();
    test_jump_above_15_ticks();
    test_jump_when_several_ticks_passed();
    test_first_sight_does_not_glide();
    test_new_id_in_slot_forgets_old();
    test_reset_after_ids_restart();
    test_facing_north_keeps_last();
    test_rest_breathing_offset();
    test_bite_frame();
    TEST_SUMMARY();
}
