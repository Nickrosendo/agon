/*
 * The tactic watch (tactics.h). Every count lives in a family's row; a row is
 * found by lineage through a small index, so the watch costs little on the
 * many decisions of a tick.
 */
#include "tactics.h"

#include <stdlib.h> /* calloc, free */
#include <string.h> /* memset */

#include "chronicle.h"
#include "genome.h"
#include "world.h"

/* A chance level or another family's share below this is taken as this, so a
 * habit nobody else has gets a large but finite lift, not a division by 0. */
#define TACTIC_FLOOR 0.05f
bool tactics_init(TacticWatch *t)
{
    *t = (TacticWatch){0};
    /* calloc: zeroed memory for every row. */
    t->rows = calloc(TACTIC_FAMILIES, sizeof(TacticFamily));
    if (t->rows == NULL) {
        return false;
    }
    tactics_clear(t);
    return true;
}

void tactics_clear(TacticWatch *t)
{
    if (t->rows == NULL) {
        return;
    }
    memset(t->rows, 0, TACTIC_FAMILIES * sizeof(TacticFamily));
    for (int i = 0; i < TACTIC_INDEX; i++) {
        t->index[i] = -1;
    }
    t->seen = 0;
    memset(t->best_lift, 0, sizeof(t->best_lift));
    t->lone_share = 0.0f;
}

void tactics_free(TacticWatch *t)
{
    free(t->rows);
    t->rows = NULL;
}

/* The row of a family, or NULL if it has none. */
static TacticFamily *find_row(TacticWatch *t, uint32_t lineage)
{
    int guess = t->index[lineage % TACTIC_INDEX];
    if (guess >= 0 && t->rows[guess].used && t->rows[guess].lineage == lineage) {
        return &t->rows[guess];
    }
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        if (t->rows[i].used && t->rows[i].lineage == lineage) {
            t->index[lineage % TACTIC_INDEX] = (int16_t)i;
            return &t->rows[i];
        }
    }
    return NULL;
}

/* The row of c's family, made if it has none: in a free row, else over the row
 * with fewest members, the least followed. */
static TacticFamily *row_for(TacticWatch *t, const Creature *c)
{
    TacticFamily *row = find_row(t, c->lineage);
    if (row != NULL) {
        return row;
    }
    int pick = -1;
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        if (!t->rows[i].used) {
            pick = i;
            break;
        }
        if (pick < 0 || t->rows[i].member_ticks < t->rows[pick].member_ticks) {
            pick = i;
        }
    }
    row = &t->rows[pick];
    *row = (TacticFamily){.used = true,
                          .lineage = c->lineage,
                          .hunters = creature_is_hunter(c)};
    t->index[c->lineage % TACTIC_INDEX] = (int16_t)pick;
    return row;
}

static bool related(const Creature *a, const Creature *b, const Settings *s)
{
    return genome_kinship(&a->genome, &b->genome) >= s->kin_sense;
}

/* The living creature with this id within radius cells (a square) of (x, y), or
 * NULL. Whoever hit a creature in the last gang_window ticks stood beside it
 * then and moves at most a cell a tick, so a small square is enough. */
static const Creature *find_near(const World *w, const Pool *p, int x, int y,
                                 int radius, uint32_t id)
{
    for (int dy = -radius; dy <= radius; dy++) {
        for (int dx = -radius; dx <= radius; dx++) {
            int other = w->occupant[world_idx(w, x + dx, y + dy)];
            if (other >= 0 && p->items[other].alive && p->items[other].id == id) {
                return &p->items[other];
            }
        }
    }
    return NULL;
}

/* How many cells around a hitter can have gone since its hit: see find_near. */
static int hitter_radius(const Settings *s)
{
    int r = s->gang_window + 1;
    return r < 12 ? r : 12;
}

/* How many distinct creatures related to kin_of hit target in the last
 * gang_window ticks, not counting skip. */
static int related_hitters(const World *w, const Pool *p, const Creature *target,
                           const Creature *kin_of, uint32_t skip,
                           const Settings *s)
{
    uint32_t kept = target->hits_taken < HITS_KEPT ? target->hits_taken : HITS_KEPT;
    uint32_t counted[HITS_KEPT];
    int n = 0;
    for (uint32_t i = 0; i < kept; i++) {
        const Hit *h = &target->recent_hits[i];
        /* Unsigned subtraction: the true distance even across the wrap at 2^32. */
        if (h->by == skip || p->now - h->tick > (uint32_t)s->gang_window) {
            continue;
        }
        bool repeat = false;
        for (int j = 0; j < n; j++) {
            repeat = repeat || counted[j] == h->by;
        }
        if (repeat) {
            continue;
        }
        const Creature *hitter =
            find_near(w, p, target->x, target->y, hitter_radius(s), h->by);
        if (hitter != NULL && related(hitter, kin_of, s)) {
            counted[n] = h->by;
            n++;
        }
    }
    return n;
}

/* Whether target hit a creature in the last gang_window ticks that is related to
 * kin_of and stands within KIN_NEAR of kin_of. */
static bool hit_a_relative(const World *w, const Pool *p, const Creature *target,
                           const Creature *kin_of, const Settings *s)
{
    for (int dy = -KIN_NEAR; dy <= KIN_NEAR; dy++) {
        for (int dx = -KIN_NEAR; dx <= KIN_NEAR; dx++) {
            int other = w->occupant[world_idx(w, kin_of->x + dx, kin_of->y + dy)];
            if (other < 0) {
                continue;
            }
            const Creature *r = &p->items[other];
            if (r == kin_of || r == target || !related(r, kin_of, s)) {
                continue;
            }
            uint32_t kept = r->hits_taken < HITS_KEPT ? r->hits_taken : HITS_KEPT;
            for (uint32_t i = 0; i < kept; i++) {
                const Hit *h = &r->recent_hits[i];
                if (h->by == target->id &&
                    p->now - h->tick <= (uint32_t)s->gang_window) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* How many relatives of c stand within KIN_NEAR of it, and in *toughest whether
 * c is tougher than every one of them. */
static int relatives_near(const World *w, const Pool *p, const Creature *c,
                          const Settings *s, bool *toughest)
{
    float mine = creature_size(c, s) * (1.0f + c->genome.g[GENE_ARMOUR]);
    int n = 0;
    *toughest = true;
    for (int dy = -KIN_NEAR; dy <= KIN_NEAR; dy++) {
        for (int dx = -KIN_NEAR; dx <= KIN_NEAR; dx++) {
            int other = w->occupant[world_idx(w, c->x + dx, c->y + dy)];
            if (other < 0 || &p->items[other] == c) {
                continue;
            }
            const Creature *r = &p->items[other];
            if (!related(r, c, s)) {
                continue;
            }
            n++;
            float theirs = creature_size(r, s) * (1.0f + r->genome.g[GENE_ARMOUR]);
            if (theirs > mine) {
                *toughest = false;
            }
        }
    }
    return n;
}

/* Whether a hit landing now on target starts a fight, as the chronicle counts
 * it: its first hit, or the first after FIGHT_GAP quiet ticks. */
static bool fight_starts(const Creature *target, uint32_t now)
{
    if (target->hits_taken == 0) {
        return true;
    }
    const Hit *last = &target->recent_hits[(target->hits_taken - 1) % HITS_KEPT];
    return now - last->tick > FIGHT_GAP;
}

void tactics_note_hit(TacticWatch *t, const World *w, const Pool *p,
                      const Creature *by, const Creature *target, bool killed,
                      const Settings *s)
{
    if (t == NULL || t->rows == NULL) {
        return;
    }
    bool dark = p->light < TACTIC_DARK;
    /* The target's family first, and done with before the attacker's row is
     * found: making that row could reuse this one. */
    TacticFamily *tf = row_for(t, target);
    if (fight_starts(target, p->now)) {
        tf->attacks_on += 1.0f;
    }
    if (killed) {
        tf->deaths += 1.0f;
        tf->deaths_dark += dark ? 1.0f : 0.0f;
    }
    if (!creature_is_hunter(target)) {
        bool toughest;
        int kin = relatives_near(w, p, target, s, &toughest);
        if (kin > 0) {
            tf->shield_hits += 1.0f;
            tf->shield_expected += 1.0f / (float)(kin + 1);
            tf->shield_toughest += toughest ? 1.0f : 0.0f;
        }
    }

    TacticFamily *bf = row_for(t, by);
    if (creature_is_hunter(by)) {
        if (killed) {
            bf->kills += 1.0f;
            bf->kills_dark += dark ? 1.0f : 0.0f;
            if (related_hitters(w, p, target, by, by->id, s) > 0) {
                bf->kills_pack += 1.0f;
            }
            bool unused;
            if (relatives_near(w, p, target, s, &unused) == 0) {
                bf->kills_lone += 1.0f;
            }
        }
    } else if (hit_a_relative(w, p, target, by, s) &&
               related_hitters(w, p, target, by, by->id, s) == 1) {
        /* One relative struck this attacker already, and this hit makes two:
         * counted once, when the second joins. */
        bf->defended += 1.0f;
    }
}

void tactics_note_decision(TacticWatch *t, const Creature *c, const Senses *in,
                           bool calling, float light)
{
    if (t == NULL || t->rows == NULL || creature_asleep(c)) {
        return;
    }
    TacticFamily *row = row_for(t, c);
    bool hunter_seen = false;
    for (int ray = 0; ray < RAY_COUNT; ray++) {
        const float *v = &in->v[SENSE_RAYS + ray * RAY_VALUES];
        hunter_seen = hunter_seen || (v[RAY_NEAR] > 0.0f && v[RAY_HUNTER] > 0.5f);
    }
    float call = calling ? 1.0f : 0.0f;
    if (hunter_seen) {
        row->seen_decisions += 1.0f;
        row->seen_calls += call;
    } else {
        row->quiet_decisions += 1.0f;
        row->quiet_calls += call;
    }
    if (light < TACTIC_DARK) {
        row->dark_decisions += 1.0f;
        row->dark_kin += in->v[SENSE_KIN_NEAR];
    } else {
        row->light_decisions += 1.0f;
        row->light_kin += in->v[SENSE_KIN_NEAR];
    }
}

/* a / b, or 0 when b is not above 0. */
static float share(float a, float b)
{
    return b > 0.0f ? a / b : 0.0f;
}

static float floored(float v)
{
    return v > TACTIC_FLOOR ? v : TACTIC_FLOOR;
}

/* Totals over the families of one diet, to compare one family with the rest. */
typedef struct {
    float kills, kills_pack, member_ticks;
    float attacks_on, deaths, defended;
} DietTotals;

static void add_totals(DietTotals *d, const TacticFamily *f)
{
    d->kills += f->kills;
    d->kills_pack += f->kills_pack;
    d->member_ticks += f->member_ticks;
    d->attacks_on += f->attacks_on;
    d->deaths += f->deaths;
    d->defended += f->defended;
}

/* Whether a hunter family kills more per member than the other hunter families,
 * by tactic_useful. With no other hunters to compare with, it cannot be judged
 * and counts as useful. */
static bool kills_better(const TacticFamily *f, const DietTotals *all,
                         const Settings *s)
{
    float other_ticks = all->member_ticks - f->member_ticks;
    if (other_ticks <= 0.0f) {
        return true;
    }
    float mine = share(f->kills, f->member_ticks);
    float theirs = (all->kills - f->kills) / other_ticks;
    return mine >= s->tactic_useful * theirs && mine > 0.0f;
}

/* Whether a family loses fewer to attacks, per attack, than the other families
 * of its diet, by tactic_useful. Not judged with no others to compare with. */
static bool loses_fewer(const TacticFamily *f, const DietTotals *all,
                        const Settings *s)
{
    float other_attacks = all->attacks_on - f->attacks_on;
    if (other_attacks <= 0.0f) {
        return true;
    }
    float theirs = (all->deaths - f->deaths) / other_attacks;
    float mine = share(f->deaths, f->attacks_on);
    return theirs > 0.0f && mine * s->tactic_useful <= theirs;
}

/* Works out one family's lifts and which tactics hold. */
static uint8_t judge(TacticFamily *f, const DietTotals *hunt,
                     const DietTotals *plant, float lone_share, const Settings *s)
{
    float min = (float)s->tactic_min;
    uint8_t holds = 0;
    for (int k = 0; k < TACTIC_COUNT; k++) {
        f->lift[k] = 0.0f;
    }
    if (f->members == 0) {
        return 0;
    }
    if (f->hunters) {
        bool useful = kills_better(f, hunt, s);
        f->lift[TACTIC_NIGHT_HUNT] =
            share(f->kills_dark, f->kills) / TACTIC_DARK_SHARE;
        float others_pack = share(hunt->kills_pack - f->kills_pack,
                                  hunt->kills - f->kills);
        f->lift[TACTIC_PACK_HUNT] =
            share(f->kills_pack, f->kills) / floored(others_pack);
        f->lift[TACTIC_STRAYS] = share(f->kills_lone, f->kills) / floored(lone_share);
        for (int k = TACTIC_NIGHT_HUNT; k <= TACTIC_STRAYS; k++) {
            if (f->kills >= min && f->lift[k] >= s->tactic_lift && useful) {
                holds |= (uint8_t)(1u << k);
            }
        }
    } else {
        bool useful = loses_fewer(f, plant, s);
        float others_def = share(plant->defended - f->defended,
                                 plant->attacks_on - f->attacks_on);
        f->lift[TACTIC_DEFENCE] =
            share(f->defended, f->attacks_on) / floored(others_def);
        if (f->attacks_on >= min && f->lift[TACTIC_DEFENCE] >= s->tactic_lift &&
            useful) {
            holds |= 1u << TACTIC_DEFENCE;
        }
        f->lift[TACTIC_SHIELD] = share(f->shield_toughest, f->shield_expected);
        if (f->shield_hits >= min && f->lift[TACTIC_SHIELD] >= s->tactic_lift &&
            useful) {
            holds |= 1u << TACTIC_SHIELD;
        }
        float dark_kin = share(f->dark_kin, f->dark_decisions);
        float light_kin = share(f->light_kin, f->light_decisions);
        f->lift[TACTIC_HUDDLE] = dark_kin / floored(light_kin);
        /* Useful: fewer of its deaths in the dark than the dark's share of time.
         * A family that lost no one has nothing to show against it. */
        bool safer = f->deaths < 1.0f ||
                     share(f->deaths_dark, f->deaths) * s->tactic_useful <=
                         TACTIC_DARK_SHARE;
        if (f->dark_decisions >= min && f->light_decisions >= min &&
            f->lift[TACTIC_HUDDLE] >= s->tactic_lift && safer) {
            holds |= 1u << TACTIC_HUDDLE;
        }
    }
    /* Alarm calls, for either diet: calls per decision with a hunter in sight
     * against without one. */
    float seen_rate = share(f->seen_calls, f->seen_decisions);
    float quiet_rate = share(f->quiet_calls, f->quiet_decisions);
    f->lift[TACTIC_ALARM] = seen_rate / floored(quiet_rate);
    bool alarm_useful = f->hunters ? kills_better(f, hunt, s) : loses_fewer(f, plant, s);
    if (f->seen_calls >= min && f->lift[TACTIC_ALARM] >= s->tactic_lift &&
        alarm_useful) {
        holds |= 1u << TACTIC_ALARM;
    }
    return holds;
}

static void fade_row(TacticFamily *f, float k)
{
    f->member_ticks *= k;
    f->kills *= k;
    f->kills_dark *= k;
    f->kills_pack *= k;
    f->kills_lone *= k;
    f->attacks_on *= k;
    f->deaths *= k;
    f->deaths_dark *= k;
    f->defended *= k;
    f->shield_toughest *= k;
    f->shield_expected *= k;
    f->shield_hits *= k;
    f->seen_decisions *= k;
    f->seen_calls *= k;
    f->quiet_decisions *= k;
    f->quiet_calls *= k;
    f->dark_decisions *= k;
    f->dark_kin *= k;
    f->light_decisions *= k;
    f->light_kin *= k;
}

/* The counts left after one check: a share of stats_every / ticks_per_year
 * fades each time, so after a year about a third of an old count is left. */
static float fade_factor(const Settings *s)
{
    float k = 1.0f - (float)s->stats_every / (float)s->ticks_per_year;
    return k > 0.0f ? k : 0.0f;
}

void tactics_check(TacticWatch *t, const World *w, Pool *p,
                   struct Chronicle *ch, const Settings *s)
{
    (void)ch; /* the stories come with the next task */
    if (t == NULL || t->rows == NULL) {
        return;
    }
    float k = fade_factor(s);
    int hunters[TACTIC_FAMILIES] = {0};
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        fade_row(&t->rows[i], k);
        t->rows[i].members = 0;
    }
    /* Members, and the share of lone plant-eaters, from at most 256 of them:
     * every step-th, so a crowded world costs no more than a small one. */
    int plant_eaters = 0;
    for (int i = 0; i < p->capacity; i++) {
        const Creature *c = &p->items[i];
        if (c->alive && !creature_is_hunter(c)) {
            plant_eaters++;
        }
    }
    int step = plant_eaters / 256 + 1;
    int seen_plants = 0, sampled = 0, lone = 0;
    for (int i = 0; i < p->capacity; i++) {
        const Creature *c = &p->items[i];
        if (!c->alive) {
            continue;
        }
        TacticFamily *row = row_for(t, c);
        row->members++;
        if (creature_is_hunter(c)) {
            hunters[row - t->rows]++;
        } else if (seen_plants++ % step == 0) {
            bool unused;
            sampled++;
            lone += relatives_near(w, p, c, s, &unused) == 0;
        }
    }
    t->lone_share = share((float)lone, (float)sampled);

    DietTotals hunt = {0}, plant = {0};
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        TacticFamily *f = &t->rows[i];
        if (!f->used) {
            continue;
        }
        if (f->members == 0 && f->member_ticks < 1.0f) {
            f->used = false; /* died out and faded: the row is free again */
            continue;
        }
        f->member_ticks += (float)f->members * (float)s->stats_every;
        if (f->members > 0) {
            f->hunters = 2 * hunters[i] > f->members;
        }
        add_totals(f->hunters ? &hunt : &plant, f);
    }
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        TacticFamily *f = &t->rows[i];
        if (!f->used) {
            continue;
        }
        f->confirmed = judge(f, &hunt, &plant, t->lone_share, s);
        for (int tac = 0; tac < TACTIC_COUNT; tac++) {
            if (f->confirmed & (1u << tac)) {
                t->seen |= (uint8_t)(1u << tac);
                if (f->lift[tac] > t->best_lift[tac]) {
                    t->best_lift[tac] = f->lift[tac];
                }
            }
        }
    }
}

bool tactics_seen(const TacticWatch *t, int tactic)
{
    return (t->seen >> tactic) & 1u;
}

void tactics_reset_seen(TacticWatch *t)
{
    t->seen = 0;
    memset(t->best_lift, 0, sizeof(t->best_lift));
}

float tactics_lift(const TacticWatch *t, int tactic)
{
    return t->best_lift[tactic];
}

/* Whether a tactic belongs to a family of this diet. Alarm calls belong to both. */
static bool tactic_fits(int tactic, bool hunters)
{
    if (tactic == TACTIC_ALARM) {
        return true;
    }
    bool hunting = tactic <= TACTIC_STRAYS;
    return hunting == hunters;
}

int tactics_families(const TacticWatch *t, int tactic, int *of_diet)
{
    int showing = 0, diet = 0;
    for (int i = 0; i < TACTIC_FAMILIES; i++) {
        const TacticFamily *f = &t->rows[i];
        if (!f->used || f->members == 0 || !tactic_fits(tactic, f->hunters)) {
            continue;
        }
        diet++;
        if (f->confirmed & (1u << tactic)) {
            showing++;
        }
    }
    *of_diet = diet;
    return showing;
}

const char *tactics_name(int tactic)
{
    /* An array of string literals: each entry points at text the program keeps
     * for its whole run, so returning one is safe. */
    static const char *const names[TACTIC_COUNT] = {
        "night", "pack", "strays", "defence", "shield", "alarm", "huddle"};
    if (tactic < 0 || tactic >= TACTIC_COUNT) {
        return "?";
    }
    return names[tactic];
}
