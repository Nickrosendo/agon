#include "brain.h"

#include <string.h> /* strcmp */

/* Room for the built-in brains and a few registered at run time. */
#define BRAIN_REGISTRY_MAX 8

/*
 * The registry: pointers to brain templates, looked up by name. A static
 * array outside any function exists for the whole run. Its initialiser
 * may use &BRAIN_RANDOM and &BRAIN_NET because the address of a global
 * is known before the program starts; the rest of the array is filled
 * with NULL.
 */
static const Brain *registry[BRAIN_REGISTRY_MAX] = {&BRAIN_RANDOM,
                                                    &BRAIN_NET};
static int registry_count = 2;

bool brain_register(const Brain *tmpl)
{
    if (registry_count >= BRAIN_REGISTRY_MAX) {
        return false;
    }
    registry[registry_count] = tmpl;
    registry_count++;
    return true;
}

bool brain_open(Brain *b, const char *name, const Settings *s, uint64_t seed)
{
    *b = (Brain){0};
    for (int i = 0; i < registry_count; i++) {
        /* == on two char pointers would compare addresses; strcmp
         * compares the characters and returns 0 when they are equal. */
        if (strcmp(registry[i]->name, name) == 0) {
            /* A struct assignment copies every field: the name pointer
             * and the five function pointers. The template itself is
             * never changed, so two brains can be open at once. */
            *b = *registry[i];
            if (!b->create(b, s, seed)) {
                *b = (Brain){0};
                return false;
            }
            return true;
        }
    }
    return false;
}

void brain_close(Brain *b)
{
    /* An empty brain (never opened, failed to open, or already closed)
     * has a NULL destroy; calling a NULL function pointer would crash. */
    if (b->destroy != NULL) {
        b->destroy(b);
    }
    *b = (Brain){0};
}
