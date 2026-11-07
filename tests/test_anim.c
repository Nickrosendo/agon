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
            pool_kill(&s->pool, &s->world, i, &s->set, DEATH_WOUNDS);
        }
    }
    /* Every cell free, bare soil, so any position is allowed and nothing is
     * in reach of a bite. Killing leaves carrion behind, so this comes after. */
    for (int i = 0; i < w * h; i++) {
        s->world.terrain[i] = TERRAIN_SOIL;
        s->world.veg[i] = 0;
        s->world.carrion[i] = 0.0f;
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
    CHECK(p.action == ACTION_WALK && p.frame >= 0 && p.frame <= 3);
    /* The fraction of a tick counts too: smooth between ticks. */
    anim_pose(&a, &s, slot, 0.5f, 0, &p);
    CHECK(NEAR(p.x, 10.625f));
    /* When the move is over: on the cell, and resting. */
    s.tick += 2;
    anim_update(&a, &s, 10);
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(NEAR(p.x, 11.0f));
    CHECK(p.action == ACTION_REST && (p.frame == 0 || p.frame == 1));
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
    CHECK(p.action == ACTION_WALK && p.frame >= 0 && p.frame <= 3); /* legs still */
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
    CHECK(p.action == ACTION_REST && (p.frame == 0 || p.frame == 1));
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

    pool_kill(&s.pool, &s.world, slot, &s.set, DEATH_WOUNDS);
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
    CHECK(p1.action == ACTION_REST && p2.action == ACTION_REST);
    CHECK(p1.frame != p2.frame);
    /* A breath later, each has changed. */
    AnimPose later;
    anim_pose(&a, &s, one, 0.0f, 625, &later);
    CHECK(later.frame != p1.frame);
    anim_free(&a);
    sim_free(&s);
}

/* Mating has no picture of its own until task 11: the creature rests. */
static void test_mating_creature_rests(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    s.pool.items[slot].current.kind = ACT_MATE;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.action == ACTION_REST);
    CHECK(p.bite == BITE_NOTHING);
    anim_free(&a);
    sim_free(&s);
}

/* A plant-eater, biting, with a plant on the cell at (dx, dy) from it. */
static int biter_beside_plant(Sim *s, int dx, int dy)
{
    empty_sim(s, 64, 32);
    int slot = pool_spawn(&s->pool, &s->world, 10, 10);
    s->pool.items[slot].current.kind = ACT_BITE;
    s->pool.items[slot].genome.g[GENE_LEAF] = 1.0f;
    s->world.veg[world_idx(&s->world, 10 + dx, 10 + dy)] = 2;
    /* The starting plants' genes are random: this one must offer leaves. */
    s->world.plant[world_idx(&s->world, 10 + dx, 10 + dy)].fruit_share = 0.0f;
    return slot;
}

static void test_bite_at_nothing_shows_no_bite(void)
{
    /* A creature can hold "bite" with nothing in reach. It must not look
     * as if it were eating. */
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 10, 10);
    s.pool.items[slot].current.kind = ACT_BITE;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.bite == BITE_NOTHING);
    CHECK(p.action == ACTION_REST && (p.frame == 0 || p.frame == 1));
    anim_free(&a);
    sim_free(&s);
}

static void test_eating_a_plant_chomps_and_faces_it(void)
{
    Sim s;
    int slot = biter_beside_plant(&s, -1, 0);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p0, p1;
    anim_pose(&a, &s, slot, 0.0f, 0, &p0);
    anim_pose(&a, &s, slot, 0.0f, 150, &p1);
    CHECK(p0.bite == BITE_PLANT);
    CHECK(p0.target_x == 9 && p0.target_y == 10);
    CHECK(p0.flip); /* the plant is on its left, and it was facing right */
    /* Mouth open, then shut. */
    CHECK(p0.action != p1.action);
    CHECK(p0.action == ACTION_BITE || p1.action == ACTION_BITE);
    CHECK(p0.action == ACTION_REST || p1.action == ACTION_REST);
    anim_free(&a);
    sim_free(&s);
}

static void test_bite_straight_up_keeps_facing(void)
{
    Sim s;
    int slot = biter_beside_plant(&s, 0, -1);
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.bite == BITE_PLANT);
    CHECK(!p.flip);
    anim_free(&a);
    sim_free(&s);
}

static void test_bite_target_across_wrap(void)
{
    /* At x = 0, the plant at x = 63 is one step left, not 63 steps right. */
    Sim s;
    empty_sim(&s, 64, 32);
    int slot = pool_spawn(&s.pool, &s.world, 0, 10);
    s.pool.items[slot].current.kind = ACT_BITE;
    s.pool.items[slot].genome.g[GENE_LEAF] = 1.0f;
    s.world.veg[world_idx(&s.world, 63, 10)] = 2;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, slot, 0.0f, 0, &p);
    CHECK(p.target_x == 63 && p.target_y == 10);
    CHECK(p.flip);
    anim_free(&a);
    sim_free(&s);
}

static void test_attacker_faces_what_it_attacks(void)
{
    Sim s;
    empty_sim(&s, 64, 32);
    int attacker = pool_spawn(&s.pool, &s.world, 10, 10);
    int target = pool_spawn(&s.pool, &s.world, 11, 10);
    s.pool.items[attacker].current.kind = ACT_ATTACK;
    s.set.family_peace = 2.0f; /* both all-zero genomes: twins, otherwise spared */
    s.set.colour_peace = 0.0f;
    s.pool.items[attacker].heading = HEADING_EAST;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, attacker, 0.0f, 0, &p);
    CHECK(p.bite == POSE_ATTACK);
    CHECK(p.target_x == 11 && p.target_y == 10);
    /* Nobody ahead: a swing at nothing does not look like a fight. */
    s.pool.items[attacker].heading = HEADING_WEST;
    anim_pose(&a, &s, attacker, 0.0f, 0, &p);
    CHECK(p.bite == BITE_NOTHING);
    (void)target;
    anim_free(&a);
    sim_free(&s);
}

static void test_biter_beside_creature_and_carrion_eats(void)
{
    /* A bite is only ever a meal, whatever stands beside it. */
    Sim s;
    empty_sim(&s, 64, 32);
    int hunter = pool_spawn(&s.pool, &s.world, 10, 10);
    int prey = pool_spawn(&s.pool, &s.world, 11, 10);
    s.pool.items[hunter].current.kind = ACT_BITE;
    s.pool.items[hunter].genome.g[GENE_MEAT] = 1.0f;
    s.pool.items[prey].genome.g[GENE_LEAF] = 1.0f;
    s.world.carrion[world_idx(&s.world, 9, 10)] = 5.0f;
    Anim a;
    CHECK(anim_init(&a, s.pool.capacity));
    anim_update(&a, &s, 10);
    AnimPose p;
    anim_pose(&a, &s, hunter, 0.0f, 0, &p);
    CHECK(p.bite == BITE_CARRION);
    CHECK(p.target_x == 9 && p.target_y == 10);
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
    test_mating_creature_rests();
    test_bite_at_nothing_shows_no_bite();
    test_eating_a_plant_chomps_and_faces_it();
    test_bite_straight_up_keeps_facing();
    test_bite_target_across_wrap();
    test_attacker_faces_what_it_attacks();
    test_biter_beside_creature_and_carrion_eats();
    TEST_SUMMARY();
}
