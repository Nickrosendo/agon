/*
 * pose: how a body holds itself in each frame of an action's cycle. The
 * picture cache asks it for every picture it draws, and the window asks it
 * which frame of a cycle a creature is in at a given body time, and whether
 * its eyes are open. A port of the mockup's pose() (attack-v9.html, lines
 * 160-235) with every cycle cut into a whole number of frames, so each loops
 * without a jump.
 *
 * The attack stands until its own task; everything else moves.
 */
#ifndef AGON_POSE_H
#define AGON_POSE_H

#include <stdint.h>

#include "sprite.h" /* SpriteBody, SpritePose, SPRITE_FRAMES */

/* How long one cycle of each action with a fixed length takes, in seconds.
 * Walking and running depend on the body: see pose_cycle_seconds. */
#define POSE_REST_SECONDS 6.0f
#define POSE_GRAZE_SECONDS 3.0f
#define POSE_BITE_SECONDS 1.3f
#define POSE_SLEEP_SECONDS 3.0f
/* A run's steps come this many times as often as a walk's (v9:174). */
#define POSE_RUN_RATE 1.9f
/* The walk and run cycle may be sped up this much to keep up with the glide. */
#define POSE_CADENCE_MAX 3.0f

/* The pose of body b in frame `frame` of the action's cycle (0 ..
 * SPRITE_FRAMES[action] - 1; others are brought back into range). */
void pose_at(SpritePose *out, const SpriteBody *b, int action, int frame);

/* How long the action's cycle takes for this body at its own pace, in
 * seconds: a walk is one step cycle at the species' step rate, a run 1.9
 * times faster. 0 for an action that has no cycle yet (the attack). b is read
 * only for walking and running, so it may be NULL for the others. */
float pose_cycle_seconds(const SpriteBody *b, int action);

/* How many times faster than its own pace a walk or run cycle has to go so
 * the feet keep up with a body gliding cells_per_second: the glide in art
 * pixels (16 a cell) over two strides a cycle, kept between 1 and
 * POSE_CADENCE_MAX. 1 for every other action. */
float pose_cadence(const SpriteBody *b, int action, float cells_per_second);

/* The frame of the action's cycle at body time ms, for a cycle that takes
 * cycle_seconds. The id shifts each creature's cycle by a different share of
 * it, so a herd is out of step. 0 for an action out of range or a cycle that
 * is not a positive length. */
int pose_frame(uint64_t ms, uint32_t id, int action, float cycle_seconds);

/* The eyes at body time ms: 1 open, 0.4 half, 0 shut. A blink every 3.6 s,
 * half shut for 60 ms, shut for 60 ms, half shut for 60 ms (v9:171); the id
 * shifts when it comes, as the mockup's seed does. */
float pose_eyes(uint64_t ms, uint32_t id);

/* The mockup's easing curves (v9:160-163). u, and x between a and b, run 0..1. */
float pose_ease_in_out(float u);
float pose_ease_out(float u);
float pose_ease_back(float u);  /* overshoots past 1 before it settles */
float pose_smooth(float a, float b, float x);

#endif
