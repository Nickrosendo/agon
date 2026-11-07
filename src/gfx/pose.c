#include "pose.h"

#include <math.h> /* sinf */

#define TWO_PI 6.28318531f

/* Asleep (v9:231-233): legs folded under, eyes shut, the neck lowered, the
 * head drawn back, the ears laid down, one slow breath per cycle. u is the
 * share of the cycle, 0 up to 1. */
static void pose_sleep(SpritePose *p, float u)
{
    p->action = ACTION_SLEEP;
    p->fold = 1.0f;
    p->eyes = 0.0f;
    p->neck = -0.9f;
    p->head_dx = -0.5f;
    p->trunk = -0.3f;
    p->tail = 0.5f;
    p->ear = 0.8f;
    /* The mockup breathes with sin(t * 1.1), which does not fit a whole number
     * of times into a 3 second loop and would jump where the loop starts
     * again. One breath per cycle keeps its depth and loops cleanly. */
    p->breath = (0.5f + 0.5f * sinf(TWO_PI * u)) * 0.45f;
}

void pose_at(SpritePose *out, const SpriteBody *b, int action, int frame)
{
    (void)b; /* the cycles of the animation stage will depend on the body */
    skeleton_pose_standing(out);
    if (action != ACTION_SLEEP) {
        return;
    }
    int n = SPRITE_FRAMES[ACTION_SLEEP];
    frame = (frame % n + n) % n; /* % keeps a negative sign: see sprite.c */
    pose_sleep(out, (float)frame / (float)n);
}

float pose_cycle_seconds(int action)
{
    return action == ACTION_SLEEP ? POSE_SLEEP_SECONDS : 0.0f;
}

int pose_frame(uint64_t ms, uint32_t id, int action)
{
    if (action < 0 || action >= ACTION_COUNT) {
        return 0;
    }
    float seconds = pose_cycle_seconds(action);
    if (!(seconds > 0.0f)) {
        return 0;
    }
    uint64_t n = (uint64_t)SPRITE_FRAMES[action];
    uint64_t cycle_ms = (uint64_t)(seconds * 1000.0f);
    /* How far through the cycle the clock is, as a frame number. */
    uint64_t by_time = (ms % cycle_ms) * n / cycle_ms;
    /* The id's shift: id times 2654435769 (2^32 divided by the golden ratio),
     * wrapping at 2^32, is a share of the cycle spread evenly over ids, with
     * neighbouring ids far apart. Multiplying by n and keeping the top 32 bits
     * turns that share into a frame. The uint32_t multiply wraps on purpose:
     * unsigned overflow is defined in C, signed overflow is not. */
    uint32_t share = id * 2654435769u;
    uint64_t by_id = ((uint64_t)share * n) >> 32;
    return (int)((by_time + by_id) % n);
}
