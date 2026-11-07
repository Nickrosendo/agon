/*
 * pose: how a body holds itself in each frame of an action's cycle. The
 * picture cache asks it for every picture it draws, and the window asks it
 * which frame of a cycle a creature is in at a given body time.
 *
 * Today only sleep has a pose of its own (the mockup's, v9:231-233); every
 * other action stands, until the animation stage gives each its cycle.
 */
#ifndef AGON_POSE_H
#define AGON_POSE_H

#include <stdint.h>

#include "sprite.h" /* SpriteBody, SpritePose, SPRITE_FRAMES */

/* One slow breath while asleep takes this long; its SPRITE_FRAMES[ACTION_SLEEP]
 * frames are spread evenly over it. */
#define POSE_SLEEP_SECONDS 3.0f

/* The pose of body b in frame `frame` of the action's cycle (0 ..
 * SPRITE_FRAMES[action] - 1; others are brought back into range). */
void pose_at(SpritePose *out, const SpriteBody *b, int action, int frame);

/* How long the action's cycle takes, in seconds; 0 for an action that has no
 * cycle yet. */
float pose_cycle_seconds(int action);

/* The frame of the action's cycle at body time ms. The id shifts each
 * creature's cycle by a different share of it, so a herd is out of step. 0 for
 * an action with no cycle or out of range. */
int pose_frame(uint64_t ms, uint32_t id, int action);

#endif
