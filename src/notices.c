#include "notices.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* memset */

#include "actions.h"
#include "chronicle.h"
#include "creature.h" /* creature_size */
#include "genome.h"
#include "lifepage.h" /* life_find_slot */
#include "tactics.h"

const int NOTICE_GENE_LIST[] = {
    GENE_SIZE, GENE_SPEED, GENE_SIGHT, GENE_ARMOUR, GENE_WEAPON,
    GENE_LEGS, GENE_SWIM, GENE_NIGHT_EYES, GENE_LEARN,
};
_Static_assert(sizeof(NOTICE_GENE_LIST) / sizeof(NOTICE_GENE_LIST[0]) ==
                   NOTICE_GENES,
               "NOTICE_GENES must match NOTICE_GENE_LIST");

/* The words for a watched gene: the givable ones from actions.c, and the two
 * more this list has. */
static const char *gene_word(int gene)
{
    if (gene == GENE_SPEED) {
        return "speed";
    }
    if (gene == GENE_SIGHT) {
        return "sight";
    }
    return actions_gene_name(gene);
}

void notices_reset(Notices *n)
{
    /* Every field 0, false or an empty string: the "nothing yet" of each. */
    *n = (Notices){0};
}

void notices_add(Notices *n, uint64_t tick, uint32_t creature,
                 const char *text)
{
    Notice *x = &n->items[n->total % NOTICES_KEPT];
    x->tick = tick;
    x->creature = creature;
    /* snprintf with "%s" copies and always ends with '\0' inside the array,
     * cutting a longer text; strcpy would run past the end. */
    snprintf(x->text, sizeof(x->text), "%s", text);
    n->total++;
}

const Notice *notices_get(const Notices *n, uint32_t back)
{
    if (back >= n->total || back >= NOTICES_KEPT) {
        return NULL;
    }
    /* total - 1 - back cannot wrap round below 0: back < total. */
    return &n->items[(n->total - 1 - back) % NOTICES_KEPT];
}

const Notice *notices_newest(const Notices *n)
{
    return notices_get(n, 0);
}

/* "Varo of the Sodri family", from the creature if alive, else the chronicle. */
static void who_words(const Sim *s, uint32_t id, char *out, size_t len)
{
    char name[16], family[16];
    name_from_id(id, name, sizeof(name));
    int slot = life_find_slot(s, id);
    const NotableLife *life = chronicle_find(&s->chronicle, id);
    uint32_t lineage = slot >= 0       ? s->pool.items[slot].lineage
                       : life != NULL ? life->lineage
                                       : id;
    name_from_id(lineage, family, sizeof(family));
    snprintf(out, len, "%s of the %s family", name, family);
}

/* ---- giants and tactics: counts the world keeps already ---- */

static void check_giant(Notices *n, const Sim *s)
{
    const Chronicle *ch = &s->chronicle;
    /* != rather than >: after a load the chronicle counts from 0 again. */
    if (ch->giants == n->giants_seen) {
        return;
    }
    n->giants_seen = ch->giants;
    /* A giant that died at once has no body left to measure: no notice. */
    int slot = life_find_slot(s, ch->newest_giant);
    if (slot < 0) {
        return;
    }
    float size = creature_size(&s->pool.items[slot], &s->set);
    bool first = n->record_size == 0.0f;
    if (!n->started) {
        n->record_size = size; /* known already: the record to beat */
        return;
    }
    if (first) {
        n->record_size = size;
        char who[48], text[NOTICE_TEXT];
        who_words(s, ch->newest_giant, who, sizeof(who));
        snprintf(text, sizeof(text), "the first giant: %s", who);
        notices_add(n, s->tick, ch->newest_giant, text);
    }
}

/* After the first giant: a body larger than the record by NOTICE_RECORD_STEP.
 * Bodies keep growing after they become giants, so the record is looked for
 * among all the living, not only the newest giant. */
static void check_record(Notices *n, const Sim *s)
{
    if (n->record_size == 0.0f) {
        return;
    }
    const Creature *largest = NULL;
    float size = 0.0f;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        float z = c->alive ? creature_size(c, &s->set) : 0.0f;
        if (z > size) {
            size = z;
            largest = c;
        }
    }
    if (largest == NULL || size < n->record_size * (1.0f + NOTICE_RECORD_STEP)) {
        return;
    }
    n->record_size = size;
    if (!n->started) {
        return;
    }
    char who[48], text[NOTICE_TEXT];
    who_words(s, largest->id, who, sizeof(who));
    snprintf(text, sizeof(text), "the largest yet: %s, size %.1f", who,
             (double)size);
    notices_add(n, s->tick, largest->id, text);
}

/* A living member of a family showing the tactic, or NOTICE_NOBODY. */
static uint32_t tactic_member(const Sim *s, int tactic)
{
    const TacticWatch *t = &s->tactics;
    if (t->rows == NULL) {
        return NOTICE_NOBODY;
    }
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        const TacticFamily *f = &t->rows[i];
        if (!f->used || !(f->confirmed & (1u << tactic))) {
            continue;
        }
        for (int j = 0; j < s->pool.capacity; j++) {
            const Creature *c = &s->pool.items[j];
            if (c->alive && c->lineage == f->lineage) {
                return c->id;
            }
        }
    }
    return NOTICE_NOBODY;
}

static void check_tactics(Notices *n, const Sim *s)
{
    uint16_t seen = s->tactics.seen;
    /* A cleared watch (a new world) makes every tactic news again. */
    n->tactics_told &= seen;
    /* ~ flips every bit: the bits seen and not yet told. */
    uint16_t fresh = (uint16_t)(seen & ~n->tactics_told);
    n->tactics_told = seen;
    if (!n->started) {
        return;
    }
    for (int k = 0; k < TACTIC_COUNT; k++) {
        if (!(fresh & (1u << k))) {
            continue;
        }
        uint32_t id = tactic_member(s, k);
        char text[NOTICE_TEXT];
        if (id != NOTICE_NOBODY) {
            char who[48];
            who_words(s, id, who, sizeof(who));
            snprintf(text, sizeof(text), "first seen: %s, in the family of %s",
                     tactics_name(k), who);
        } else {
            snprintf(text, sizeof(text), "first seen: a family's %s",
                     tactics_name(k));
        }
        notices_add(n, s->tick, id, text);
    }
}

/* ---- big families ---- */

/* Members per lineage, counted afresh at each check in an open-addressing
 * table: a lineage goes in the slot its hash names, or the next free one.
 * static: 32 KB kept off the stack, which is small in the web build. The
 * cost is that only one check may use it at a time, which with one thread
 * is always so. */
#define COUNT_BITS 12
#define COUNT_SLOTS (1 << COUNT_BITS)
static uint32_t count_lineage[COUNT_SLOTS];
static int count_members[COUNT_SLOTS]; /* 0: the slot is free */
/* Each family's watched genes summed, for its average: 4096 x 9 floats,
 * 144 KB, static for the same reason. A 2-D array is one block of rows. */
static float count_genes[COUNT_SLOTS][NOTICE_GENES];

/* The slot for lineage: its own, or the free one where it would go. */
static int count_slot(uint32_t lineage)
{
    /* Multiplying by a large odd number spreads nearby ids over the table;
     * the top COUNT_BITS bits of the 32-bit product are the slot. The product
     * wraps round, which unsigned numbers are defined to do. */
    uint32_t h = (lineage * 2654435761u) >> (32 - COUNT_BITS);
    for (int tries = 0; tries < COUNT_SLOTS; tries++) {
        int slot = (int)((h + (uint32_t)tries) & (COUNT_SLOTS - 1));
        if (count_members[slot] == 0 || count_lineage[slot] == lineage) {
            return slot;
        }
    }
    return -1; /* full: cannot happen with fewer lineages than slots */
}

static void count_families(const Sim *s)
{
    memset(count_members, 0, sizeof(count_members));
    int used = 0;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (!c->alive) {
            continue;
        }
        int slot = count_slot(c->lineage);
        if (slot < 0 || (count_members[slot] == 0 && 2 * used >= COUNT_SLOTS)) {
            continue; /* over half full: probing would slow; skip new names */
        }
        if (count_members[slot] == 0) {
            count_lineage[slot] = c->lineage;
            for (int k = 0; k < NOTICE_GENES; k++) {
                count_genes[slot][k] = 0.0f;
            }
            used++;
        }
        count_members[slot]++;
        for (int k = 0; k < NOTICE_GENES; k++) {
            count_genes[slot][k] += c->genome.g[NOTICE_GENE_LIST[k]];
        }
    }
}

static int members_of(uint32_t lineage)
{
    int slot = count_slot(lineage);
    return slot >= 0 && count_lineage[slot] == lineage ? count_members[slot] : 0;
}

static void check_families(Notices *n, const Sim *s)
{
    count_families(s);
    /* The big families remembered: gone, or their peak raised. */
    for (int f = 0; f < NOTICE_FAMILIES; f++) {
        BigFamily *b = &n->families[f];
        if (b->peak == 0) {
            continue;
        }
        int now = members_of(b->lineage);
        if (now > b->peak) {
            b->peak = now;
        } else if (now == 0) {
            char family[16], text[NOTICE_TEXT];
            name_from_id(b->lineage, family, sizeof(family));
            snprintf(text, sizeof(text), "the %s family is gone (once %d strong)",
                     family, b->peak);
            notices_add(n, s->tick, NOTICE_NOBODY, text);
            b->peak = 0;
        }
    }
    /* New big families take free entries. */
    for (int slot = 0; slot < COUNT_SLOTS; slot++) {
        if (count_members[slot] < NOTICE_FAMILY_MIN) {
            continue;
        }
        uint32_t lineage = count_lineage[slot];
        int free_entry = -1;
        bool known = false;
        for (int f = 0; f < NOTICE_FAMILIES; f++) {
            if (n->families[f].peak > 0 && n->families[f].lineage == lineage) {
                known = true;
            } else if (n->families[f].peak == 0 && free_entry < 0) {
                free_entry = f;
            }
        }
        if (!known && free_entry >= 0) {
            n->families[free_entry] =
                (BigFamily){.lineage = lineage, .peak = count_members[slot]};
        }
    }
}

/* ---- bodies unlike their families ---- */

/* Needs count_families to have run at this check. */
static void check_unlike(Notices *n, const Sim *s)
{
    int count[NOTICE_GENES] = {0};
    const Creature *first[NOTICE_GENES] = {NULL};
    float family_mean[NOTICE_GENES] = {0};
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (!c->alive) {
            continue;
        }
        int slot = count_slot(c->lineage);
        if (slot < 0 || count_lineage[slot] != c->lineage ||
            count_members[slot] < NOTICE_LIKE_FAMILY) {
            continue;
        }
        for (int k = 0; k < NOTICE_GENES; k++) {
            float mean = count_genes[slot][k] / (float)count_members[slot];
            float d = c->genome.g[NOTICE_GENE_LIST[k]] - mean;
            if (d >= NOTICE_UNLIKE || d <= -NOTICE_UNLIKE) {
                if (count[k] == 0) {
                    first[k] = c;
                    family_mean[k] = mean;
                }
                count[k]++;
            }
        }
    }
    for (int k = 0; k < NOTICE_GENES; k++) {
        bool here = count[k] > 0;
        bool was_here = n->unlike_here[k];
        n->unlike_here[k] = here;
        if (!here && was_here) {
            /* The last one went: a new one is news only a year on. */
            n->unlike_news_from[k] = s->tick + (uint64_t)s->set.ticks_per_year;
        }
        if (!here || was_here || !n->started || count[k] > NOTICE_RARE ||
            s->tick < n->unlike_news_from[k]) {
            continue;
        }
        int gene = NOTICE_GENE_LIST[k];
        char who[48], text[NOTICE_TEXT];
        who_words(s, first[k]->id, who, sizeof(who));
        /* The casts: printf's %f takes a double, and a float passed through
         * "..." becomes one anyway; saying so keeps -Wconversion quiet. */
        snprintf(text, sizeof(text), "unlike its family: %s, %s %.2f (family %.2f)",
                 who, gene_word(gene), (double)first[k]->genome.g[gene],
                 (double)family_mean[k]);
        notices_add(n, s->tick, first[k]->id, text);
    }
}

/* ---- traits the player gave ---- */

/* A living carrier of the gift, for the notice to follow. */
static uint32_t a_carrier(const Sim *s, const PlayerAction *a)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        float d = c->genome.g[a->gene] - a->value;
        if (c->alive && d < ACTIONS_CARRIER_TOLERANCE &&
            d > -ACTIONS_CARRIER_TOLERANCE) {
            return c->id;
        }
    }
    return NOTICE_NOBODY;
}

static void check_gifts(Notices *n, const Sim *s)
{
    const ActionLog *log = &s->actions;
    /* Only entries already applied (before next) can have carriers. */
    int upto = log->next < NOTICE_GIFTS ? log->next : NOTICE_GIFTS;
    for (int i = 0; i < upto; i++) {
        const PlayerAction *a = &log->items[i];
        if (a->kind != ACTION_TRAIT || actions_gift_repeats(log, i)) {
            continue;
        }
        int now = actions_carriers(s, a->gene, a->value);
        uint8_t *state = &n->gifts[i];
        if (now > 0) {
            *state |= GIFT_CARRIED;
        }
        if (!n->started) {
            /* What is already so at the first check is known, not news. */
            *state |= now > NOTICE_GIFT_SPREAD ? GIFT_SPREAD : 0;
            continue;
        }
        char text[NOTICE_TEXT];
        if (now > NOTICE_GIFT_SPREAD && !(*state & GIFT_SPREAD)) {
            *state |= GIFT_SPREAD;
            snprintf(text, sizeof(text),
                     "the %s you gave (%.2f) spreads: %d carry it",
                     actions_gene_name(a->gene), (double)a->value, now);
            notices_add(n, s->tick, a_carrier(s, a), text);
        } else if (now == 0 && (*state & GIFT_CARRIED) &&
                   !(*state & GIFT_GONE)) {
            *state |= GIFT_GONE;
            snprintf(text, sizeof(text),
                     "the %s you gave (%.2f) died out",
                     actions_gene_name(a->gene), (double)a->value);
            notices_add(n, s->tick, NOTICE_NOBODY, text);
        }
    }
}

void notices_check(Notices *n, const Sim *s)
{
    if (n->started && s->stats.tick == n->last_sample) {
        return; /* no new sample since the last look */
    }
    n->last_sample = s->stats.tick;
    check_giant(n, s);
    check_record(n, s);
    check_tactics(n, s);
    check_families(n, s);
    check_unlike(n, s);
    check_gifts(n, s);
    n->started = true;
}
