#include "inspect.h"

#include <stddef.h> /* NULL */

/* The slot of the living creature with this id, or -1. Ids are never
 * reused within a world, so a match is the same creature. A search over
 * every slot: once per frame, which is cheap next to drawing. */
static int find_creature(const Pool *p, uint32_t id)
{
    for (int i = 0; i < p->capacity; i++) {
        if (p->items[i].alive && p->items[i].id == id) {
            return i;
        }
    }
    return -1;
}

/* Follows whatever creature is on the cursor's cell, or none. */
static void pick(Inspect *in, const Sim *s)
{
    int slot = s->world.occupant[world_idx(&s->world, in->x, in->y)];
    in->following = slot >= 0;
    if (in->following) {
        in->id = s->pool.items[slot].id;
    }
}

static int clamp(int v, int lo, int hi)
{
    if (v < lo) {
        return lo;
    }
    return v > hi ? hi : v;
}

void inspect_start(Inspect *in, const Sim *s, int max_x, int max_y)
{
    in->on = true;
    in->x = max_x / 2;
    in->y = max_y / 2;
    int best = -1;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (c->alive && c->x < max_x && c->y < max_y &&
            (best < 0 || c->age > s->pool.items[best].age)) {
            best = i;
        }
    }
    if (best >= 0) {
        in->x = s->pool.items[best].x;
        in->y = s->pool.items[best].y;
    }
    pick(in, s);
}

void inspect_move(Inspect *in, const Sim *s, int dx, int dy, int max_x,
                  int max_y)
{
    in->x = clamp(in->x + dx, 0, max_x - 1);
    in->y = clamp(in->y + dy, 0, max_y - 1);
    pick(in, s);
}

void inspect_update(Inspect *in, const Sim *s)
{
    if (!in->following) {
        return;
    }
    int slot = find_creature(&s->pool, in->id);
    if (slot < 0) {
        in->following = false; /* it died; the cursor stays put */
        return;
    }
    in->x = s->pool.items[slot].x;
    in->y = s->pool.items[slot].y;
}

const Creature *inspect_creature(const Inspect *in, const Sim *s)
{
    if (!in->following) {
        return NULL;
    }
    int slot = find_creature(&s->pool, in->id);
    return slot >= 0 ? &s->pool.items[slot] : NULL;
}
