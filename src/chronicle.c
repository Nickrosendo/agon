#include "chronicle.h"

#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* malloc, free */

#include "climate.h"
#include "years.h"

bool chronicle_init(Chronicle *c)
{
    /* sizeof(*c->lives) is one NotableLife, about 850 bytes; the table is about
     * 54 KB. On the heap rather than inside the Sim, because programs keep their
     * Sim on the stack, and the web build's stack is small. */
    c->lives = malloc(CHRONICLE_NOTABLE * sizeof(*c->lives));
    if (c->lives == NULL) {
        return false;
    }
    chronicle_clear(c);
    return true;
}

void chronicle_clear(Chronicle *c)
{
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        c->lives[i] = (NotableLife){0};
    }
}

void chronicle_free(Chronicle *c)
{
    free(c->lives); /* free(NULL) is allowed and does nothing */
    c->lives = NULL;
}

/* Adds e to a ring of cap events whose count is *noted, over its oldest once it
 * is full. The same function serves a creature's ring of 8 and an entry's of 48:
 * an array passed to a function arrives as a pointer to its first element, and
 * the function cannot tell its length, hence cap. */
static void ring_push(LifeEvent *ring, int cap, uint32_t *noted, LifeEvent e)
{
    ring[*noted % (uint32_t)cap] = e;
    (*noted)++;
}

float chronicle_notability(const LifeTally *t, uint32_t age)
{
    float score = 4.0f * (float)t->kills + 6.0f * (float)t->hunter_kills +
                  2.0f * (float)t->fights + (float)t->heavy_wounds +
                  (float)t->children + 3.0f * (float)t->moves;
    if (t->flags & TALLY_WAS_LARGEST) {
        score += 10.0f;
    }
    if (t->flags & TALLY_WAS_OLDEST) {
        score += 10.0f;
    }
    if (t->flags & TALLY_GIANT) {
        score += 25.0f;
    }
    /* One point per 1,000 ticks lived: a long quiet life counts, a little. */
    return score + (float)age / 1000.0f;
}

/* The tally's side of an event. */
static void tally_event(LifeTally *t, LifeEvent *e)
{
    switch (e->kind) {
    case LIFE_BORN:
        t->born = e->tick;
        if (e->value == 0) {
            t->flags |= TALLY_FOUNDER;
        }
        break;
    case LIFE_CHILD:
        t->children++;
        break;
    case LIFE_SURVIVED:
        t->fights++;
        break;
    case LIFE_WOUNDED:
        t->heavy_wounds++;
        break;
    case LIFE_KILL_HUNTER:
        t->hunter_kills++;
        /* No break: "falls through" into the next case, so a hunter killed is a
         * kill too. C does this whenever break is left out, wanted or not, which
         * is why the wanted ones get a comment. */
        /* fall through */
    case LIFE_KILL:
        t->kills++;
        /* Kills are at most 2^32 - 1; past 2^31 this conversion would not fit
         * an int32_t, and no creature lives that long. */
        e->value = (int32_t)t->kills;
        break;
    case LIFE_KIN_LOST:
        t->kin_lost++;
        break;
    case LIFE_MOVED:
        t->moves++;
        break;
    case LIFE_LARGEST:
        t->flags |= TALLY_WAS_LARGEST;
        break;
    case LIFE_OLDEST:
        t->flags |= TALLY_WAS_OLDEST;
        break;
    case LIFE_GIANT:
        t->flags |= TALLY_GIANT;
        break;
    default: /* LIFE_ATTACKED and LIFE_DIED change no count */
        break;
    }
}

static NotableLife *find_entry(Chronicle *c, uint32_t id)
{
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        if (c->lives[i].used && c->lives[i].id == id) {
            return &c->lives[i];
        }
    }
    return NULL;
}

const NotableLife *chronicle_find(const Chronicle *c, uint32_t id)
{
    /* The cast drops const only to share find_entry, which writes nothing;
     * the result goes back out as a pointer to const. */
    return find_entry((Chronicle *)c, id);
}

/* The entry with the lowest score, unused entries first. Of equal scores the
 * first wins. */
static NotableLife *least_notable(Chronicle *c)
{
    NotableLife *least = &c->lives[0];
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        NotableLife *n = &c->lives[i];
        if (!n->used) {
            return n;
        }
        if (n->score < least->score) {
            least = n;
        }
    }
    return least;
}

/* Copies the parts of who that an entry shows. */
static void entry_update(NotableLife *n, const Creature *who, float score)
{
    n->used = true;
    n->alive = who->alive;
    n->id = who->id;
    n->lineage = who->lineage;
    n->age = who->age;
    n->score = score;
    n->tally = who->tally; /* a struct assignment copies every field */
}

/* A new entry: who's details, and the events still in its ring, oldest first. */
static void entry_claim(NotableLife *n, const Creature *who, float score)
{
    *n = (NotableLife){0};
    entry_update(n, who, score);
    uint32_t kept = who->events_noted < CREATURE_EVENTS ? who->events_noted
                                                        : CREATURE_EVENTS;
    uint32_t first = who->events_noted - kept;
    for (uint32_t k = 0; k < kept; k++) {
        ring_push(n->events, NOTABLE_EVENTS, &n->events_noted,
                  who->events[(first + k) % CREATURE_EVENTS]);
    }
}

void chronicle_note(Chronicle *c, Creature *who, LifeEvent e)
{
    if (c == NULL) {
        return;
    }
    tally_event(&who->tally, &e);
    ring_push(who->events, CREATURE_EVENTS, &who->events_noted, e);
    float score = chronicle_notability(&who->tally, who->age);
    NotableLife *n = find_entry(c, who->id);
    if (n != NULL) {
        entry_update(n, who, score);
        ring_push(n->events, NOTABLE_EVENTS, &n->events_noted, e);
    } else {
        NotableLife *least = least_notable(c);
        /* Strictly above: at equal scores, whoever is in the table stays. */
        if (!least->used || score > least->score) {
            entry_claim(least, who, score); /* e is already in who's ring */
        } else {
            return;
        }
        n = least;
    }
    /* The death is noted while the creature is still alive in its slot, so the
     * entry is told here. */
    if (e.kind == LIFE_DIED) {
        n->alive = false;
    }
}

/* ---- names and text ---- */

/* Mixes the bits of a number so that ids 1, 2, 3 give unrelated results: the
 * finishing step of the "splitmix64" generator. Arithmetic on uint64_t wraps
 * modulo 2^64, which is defined for unsigned types. */
static uint64_t mix(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

/* An array of pointers to string constants: each entry is the address of text
 * the compiler stores once, read-only. */
static const char *const ONSETS[] = {"b", "d",  "g",  "k",  "l",  "m",
                                     "n", "r",  "s",  "t",  "v",  "z",
                                     "th", "sh", "dr", "kr"};
static const char *const VOWELS[] = {"a", "e", "i", "o", "u", "ae", "ai", "ou"};
static const char *const CODAS[] = {"", "", "", "n", "r", "s", "l", "th"};

/* The number of entries in an array, worked out by the compiler: the whole
 * array's size over one entry's. Only for a real array, never for a pointer,
 * where sizeof gives the pointer's own size. */
#define COUNT_OF(a) (sizeof(a) / sizeof((a)[0]))

void name_from_id(uint32_t id, char *out, size_t len)
{
    if (len == 0) {
        return;
    }
    uint64_t bits = mix(id);
    int syllables = 2 + (int)(bits % 2); /* two or three */
    bits /= 2;
    size_t used = 0;
    out[0] = '\0';
    for (int k = 0; k < syllables; k++) {
        const char *onset = ONSETS[bits % COUNT_OF(ONSETS)];
        bits /= COUNT_OF(ONSETS);
        const char *vowel = VOWELS[bits % COUNT_OF(VOWELS)];
        bits /= COUNT_OF(VOWELS);
        /* Only the last syllable may close on a consonant. */
        const char *coda = "";
        if (k == syllables - 1) {
            coda = CODAS[bits % COUNT_OF(CODAS)];
            bits /= COUNT_OF(CODAS);
        }
        /* snprintf writes at most len - used bytes, the '\0' included, and
         * returns how many it wanted to write. Past the end, stop. */
        int wrote = snprintf(out + used, len - used, "%s%s%s", onset, vowel, coda);
        if (wrote < 0 || (size_t)wrote >= len - used) {
            break;
        }
        used += (size_t)wrote;
    }
    /* A capital first letter. Every name starts with a lowercase ASCII letter,
     * and in ASCII a capital is its lowercase letter minus 32. */
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 32);
    }
}

const char *chronicle_title(const LifeTally *t)
{
    if (t->flags & TALLY_GIANT) {
        return "the Giant";
    }
    if (t->hunter_kills >= 3) {
        return "Breaker of Hunters";
    }
    if (t->kills >= 10) {
        return "the Slayer";
    }
    if (t->fights >= 10) {
        return "the Scarred";
    }
    if (t->children >= 30) {
        return "Parent of Many";
    }
    if (t->moves >= 3) {
        return "the Wanderer";
    }
    if (t->flags & TALLY_WAS_OLDEST) {
        return "the Elder";
    }
    return "";
}

static const char *region_words(int region)
{
    switch (region) {
    case REGION_COLD:
        return "the cold lands";
    case REGION_HOT_DRY:
        return "the hot dry lands";
    case REGION_TEMPERATE:
        return "the mild lands";
    case REGION_WET:
        return "the wet lands";
    case REGION_SHALLOW:
        return "the shallows";
    default:
        return "unknown ground";
    }
}

static const char *death_words(int cause)
{
    switch (cause) {
    case DEATH_WOUNDS:
        return "died of wounds";
    case DEATH_HUNGER:
        return "died of hunger";
    case DEATH_AGE:
        return "reached the age limit and died";
    default:
        return "died";
    }
}

/* Writes the text of the event without its date. */
static void event_words(const LifeEvent *e, const LifeTally *t, char *out,
                        size_t len)
{
    /* Names are short: three syllables of at most 2 + 2 letters, a coda of 2. */
    char other[16], second[16];
    name_from_id(e->other_id, other, sizeof(other));
    switch (e->kind) {
    case LIFE_BORN:
        if (e->value == 0) {
            snprintf(out, len, "came into the world, a founder");
        } else {
            name_from_id(t->parents[0], other, sizeof(other));
            name_from_id(t->parents[1], second, sizeof(second));
            snprintf(out, len, "born to %s and %s", other, second);
        }
        break;
    case LIFE_CHILD:
        snprintf(out, len, "had a child, %s", other);
        break;
    case LIFE_ATTACKED:
        snprintf(out, len, "attacked by %s", other);
        break;
    case LIFE_SURVIVED:
        snprintf(out, len, "survived a fight with %s", other);
        break;
    case LIFE_WOUNDED:
        snprintf(out, len, "badly wounded by %s, %d %% of health left", other,
                 (int)e->value);
        break;
    case LIFE_KILL:
        snprintf(out, len, "killed %s, kill %d", other, (int)e->value);
        break;
    case LIFE_KILL_HUNTER:
        snprintf(out, len, "killed the hunter %s, kill %d", other, (int)e->value);
        break;
    case LIFE_KIN_LOST:
        snprintf(out, len, "a relative, %s, died nearby", other);
        break;
    case LIFE_MOVED:
        snprintf(out, len, "moved into %s", region_words(e->value));
        break;
    case LIFE_LARGEST:
        snprintf(out, len, "became the largest alive, size %d.%d",
                 (int)e->value / 10, (int)e->value % 10);
        break;
    case LIFE_OLDEST:
        snprintf(out, len, "became the oldest alive");
        break;
    case LIFE_GIANT:
        snprintf(out, len, "grew to giant size, %d.%d", (int)e->value / 10,
                 (int)e->value % 10);
        break;
    case LIFE_DIED:
        if (e->value == DEATH_WOUNDS) {
            snprintf(out, len, "%s, last struck by %s", death_words(e->value),
                     other);
        } else {
            snprintf(out, len, "%s", death_words(e->value));
        }
        break;
    default:
        snprintf(out, len, "something unrecorded");
        break;
    }
}

void chronicle_line(const LifeEvent *e, const LifeTally *t, int ticks_per_year,
                    char *out, size_t len)
{
    if (len == 0) {
        return;
    }
    char words[96];
    event_words(e, t, words, sizeof(words));
    snprintf(out, len, "year %d: %s", years_whole(e->tick, ticks_per_year),
             words);
}
