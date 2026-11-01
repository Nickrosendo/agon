#include "settings.h"

/* Returns the struct by value: the caller gets its own copy, as if every
 * field were assigned one by one. Fine for a small struct read once. */
Settings settings_default(void)
{
    Settings s = {
        .water_level = 0.38f,
        .rock_level = 0.80f,
        .noise_scale = 48.0f,
        .noise_octaves = 4,
        .fertility_scale = 24.0f,
    };
    return s;
}
