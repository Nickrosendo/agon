#include "lifepage.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* strlen */

#include "years.h"

int life_find_slot(const Sim *s, uint32_t id)
{
    /* A scan of every slot: about 8,000 at the default pool, once a frame, which
     * costs microseconds. Ids are never reused, so a match is the right one. */
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (c->alive && c->id == id) {
            return i;
        }
    }
    return -1;
}

/* Copies the newest of a ring's events into the page, oldest first. ring has cap
 * entries and has been given noted events in all; an array parameter is only a
 * pointer, so its length has to come as an argument. */
static void copy_ring(LifePage *p, const LifeEvent *ring, int cap, uint32_t noted)
{
    uint32_t kept = noted < (uint32_t)cap ? noted : (uint32_t)cap;
    uint32_t first = noted - kept;
    for (uint32_t k = 0; k < kept; k++) {
        p->events[k] = ring[(first + k) % (uint32_t)cap];
    }
    p->n_events = (int)kept;
    p->events_noted = noted;
}

void life_page_update(LifePage *p, const Sim *s)
{
    if (!p->open) {
        return;
    }
    int slot = life_find_slot(s, p->id);
    const Creature *c = slot >= 0 ? &s->pool.items[slot] : NULL;
    const NotableLife *n = s->chronicle.lives != NULL
                               ? chronicle_find(&s->chronicle, p->id)
                               : NULL;
    if (c == NULL && n == NULL) {
        /* Gone from both: keep the last copy. A life seen alive and now missing
         * has died, even if its death came too late for the table. */
        p->alive = false;
        return;
    }
    p->known = true;
    p->in_table = n != NULL;
    if (c != NULL) {
        p->alive = true;
        p->lineage = c->lineage;
        p->age = c->age;
        p->size = creature_size(c, &s->set);
        p->health = c->health;
        p->tally = c->tally;
    } else {
        p->alive = false;
        p->lineage = n->lineage;
        p->age = n->age;
        p->size = n->tally.max_size; /* the dead are shown at their largest */
        p->health = 0.0f;
        p->tally = n->tally;
    }
    /* The table keeps 48 events, a creature's ring 8: the longer story wins. */
    if (n != NULL) {
        copy_ring(p, n->events, NOTABLE_EVENTS, n->events_noted);
    } else {
        copy_ring(p, c->events, CREATURE_EVENTS, c->events_noted);
    }
}

void life_page_open(LifePage *p, const Sim *s, uint32_t id)
{
    *p = (LifePage){.open = true, .id = id};
    life_page_update(p, s);
}

bool life_page_open_most_notable(LifePage *p, const Sim *s)
{
    *p = (LifePage){0};
    const NotableLife *best = NULL;
    for (int i = 0; s->chronicle.lives != NULL && i < CHRONICLE_NOTABLE; i++) {
        const NotableLife *n = &s->chronicle.lives[i];
        if (n->used && (best == NULL || n->score > best->score)) {
            best = n;
        }
    }
    if (best == NULL) {
        return false;
    }
    life_page_open(p, s, best->id);
    return true;
}

void life_page_close(LifePage *p)
{
    p->open = false;
}

/* Lines being written: the caller's rows, how many fit, how many so far, and the
 * width to cut each one to. */
typedef struct {
    char (*text)[LIFE_PAGE_LINE_BYTES];
    int max, n, cols;
} PageLines;

/* Adds one line, cut to cols characters; dropped when there is no room. Callers
 * format the text first with snprintf, so this takes a plain string. */
static void add(PageLines *l, const char *text)
{
    if (l->n >= l->max) {
        return;
    }
    snprintf(l->text[l->n], LIFE_PAGE_LINE_BYTES, "%s", text);
    if (l->cols >= 0 && l->cols < LIFE_PAGE_LINE_BYTES) {
        l->text[l->n][l->cols] = '\0';
    }
    l->n++;
}

/* The first lines: who, alive or dead, born of whom, and the counts. */
static void add_heading(PageLines *l, const LifePage *p, int ticks_per_year)
{
    char line[LIFE_PAGE_LINE_BYTES], name[16], family[16], age[32];
    name_from_id(p->id, name, sizeof(name));
    name_from_id(p->lineage, family, sizeof(family));
    const char *title = chronicle_title(&p->tally);
    snprintf(line, sizeof(line), " %s%s%s, of the %s family", name,
             title[0] != '\0' ? " " : "", title, family);
    add(l, line);
    years_format(age, sizeof(age), p->age, ticks_per_year);
    if (p->alive) {
        snprintf(line, sizeof(line), " alive, aged %s, size %.1f, health %.1f",
                 age, (double)p->size, (double)p->health);
    } else {
        snprintf(line, sizeof(line), " dead, aged %s, largest size %.1f", age,
                 (double)p->size);
    }
    add(l, line);
    /* The birth from the tally, which keeps it even when a killer's kills
     * have pushed the birth event out of the 48 kept. */
    int born = years_whole(p->tally.born, ticks_per_year);
    if (p->tally.flags & TALLY_FOUNDER) {
        snprintf(line, sizeof(line), " year %d: came into the world, a founder",
                 born);
    } else {
        char a[16], b[16];
        name_from_id(p->tally.parents[0], a, sizeof(a));
        name_from_id(p->tally.parents[1], b, sizeof(b));
        snprintf(line, sizeof(line), " year %d: born to %s and %s", born, a, b);
    }
    add(l, line);
    /* %u with explicit conversions, as in panel.c: uint32_t need not be
     * unsigned int on every system. */
    snprintf(line, sizeof(line),
             " kills %u (of hunters %u), fights survived %u, heavy wounds %u",
             (unsigned)p->tally.kills, (unsigned)p->tally.hunter_kills,
             (unsigned)p->tally.fights, (unsigned)p->tally.heavy_wounds);
    add(l, line);
    snprintf(line, sizeof(line),
             " children %u, kin lost nearby %u, moved to new lands %u times",
             (unsigned)p->tally.children, (unsigned)p->tally.kin_lost,
             (unsigned)p->tally.moves);
    add(l, line);
    add(l, "");
}

int life_page_text(const LifePage *p, int ticks_per_year, int cols,
                   char lines[][LIFE_PAGE_LINE_BYTES], int max_lines)
{
    PageLines l = {.text = lines, .max = max_lines, .n = 0, .cols = cols};
    if (!p->known) {
        add(&l, " nothing is recorded of this life");
        add(&l, "");
        add(&l, " L or Esc closes");
        return l.n;
    }
    add_heading(&l, p, ticks_per_year);
    /* What is left for events: the room minus the story's heading line and the
     * closing two. */
    int room = max_lines - l.n - 3;
    /* The heading has said the birth, so a story that still holds its birth
     * event starts after it. */
    int first = p->n_events > 0 && p->events[0].kind == LIFE_BORN ? 1 : 0;
    int shown = p->n_events - first;
    int left_out = 0;
    if (shown > room) {
        /* One of the rows goes to the line saying how many are left out. */
        shown = room > 1 ? room - 1 : 0;
        left_out = p->n_events - first - shown;
    }
    uint32_t lost = p->events_noted - (uint32_t)p->n_events; /* never kept */
    char line[LIFE_PAGE_LINE_BYTES];
    if (lost > 0) {
        snprintf(line, sizeof(line),
                 " story, oldest first: the newest %d of %u events are kept",
                 p->n_events, (unsigned)p->events_noted);
    } else {
        snprintf(line, sizeof(line), " story, oldest first:");
    }
    add(&l, line);
    if (left_out > 0) {
        snprintf(line, sizeof(line), "   (%d earlier events do not fit here)",
                 left_out);
        add(&l, line);
    }
    for (int k = p->n_events - shown; k < p->n_events; k++) {
        char words[LIFE_PAGE_LINE_BYTES - 4];
        chronicle_line(&p->events[k], &p->tally, ticks_per_year, words,
                       sizeof(words));
        snprintf(line, sizeof(line), "   %s", words);
        add(&l, line);
    }
    add(&l, "");
    add(&l, p->alive ? " L or Esc closes | j follows" : " L or Esc closes");
    return l.n;
}

/* ---- the giant announcement ---- */

void announce_reset(Announce *a)
{
    *a = (Announce){0};
}

bool announce_check(Announce *a, const Sim *s, char *out, size_t len)
{
    const Chronicle *ch = &s->chronicle;
    /* != rather than >: after a load the count starts again at 0. */
    if (ch->giants == a->seen) {
        return false;
    }
    a->seen = ch->giants;
    a->id = ch->newest_giant;
    a->has_giant = true;
    char name[16], family[16];
    name_from_id(a->id, name, sizeof(name));
    /* Its family from the creature, or from the table if it died at once. */
    int slot = life_find_slot(s, a->id);
    const NotableLife *n = chronicle_find(ch, a->id);
    uint32_t lineage = slot >= 0 ? s->pool.items[slot].lineage
                       : n != NULL ? n->lineage
                                   : a->id;
    name_from_id(lineage, family, sizeof(family));
    if (len > 0) {
        snprintf(out, len, "a giant: %s of the %s family, year %d (j follows)",
                 name, family, years_whole(s->tick, s->set.ticks_per_year));
    }
    return true;
}

int announce_slot(const Announce *a, const Sim *s)
{
    return a->has_giant ? life_find_slot(s, a->id) : -1;
}
