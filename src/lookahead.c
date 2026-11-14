#include "lookahead.h"

#include <stdio.h> /* snprintf */

#include "chronicle.h" /* chronicle_find, name_from_id */
#include "save.h"      /* sim_copy */

void lookahead_init(Lookahead *l, uint64_t lead)
{
    *l = (Lookahead){0};
    l->lead = lead;
}

/* The copy was made from this world, nothing has been done to it since, and
 * it has not been put back in time (a load, or a new world at the same tick
 * would also change the seed or the tick). */
static bool still_the_future(const Lookahead *l, const Sim *shown)
{
    return l->has && shown->seed == l->seed && shown->tick >= l->copied_at &&
           shown->actions.count == l->actions_seen;
}

bool lookahead_ready(const Lookahead *l, const Sim *shown)
{
    if (!still_the_future(l, shown)) {
        return false;
    }
    return l->ahead.tick >= shown->tick + l->lead || sim_is_over(&l->ahead);
}

/* A fresh copy of shown, if the old one is gone or wrong. False when memory
 * ran out; the Lookahead is empty then. */
static bool make_copy(Lookahead *l, const Sim *shown, char *err, size_t len)
{
    /* A copy fallen behind the shown world (a fast speed it could not keep
     * up with) is still right, but a new copy (about 30 ms on a window-sized
     * world) is quicker than running it up to where the shown world is. */
    if (still_the_future(l, shown) && l->ahead.tick >= shown->tick) {
        return true;
    }
    lookahead_free(l);
    /* sim_copy fills l->ahead only on success, so on failure has stays false
     * and there is nothing to free. */
    if (!sim_copy(&l->ahead, shown, err, len)) {
        return false;
    }
    l->has = true;
    l->seed = shown->seed;
    l->copied_at = shown->tick;
    l->actions_seen = shown->actions.count;
    return true;
}

/* The copy has run as far as it should. */
static bool far_enough(const Lookahead *l, const Sim *shown)
{
    return l->ahead.tick >= shown->tick + l->lead || sim_is_over(&l->ahead);
}

bool lookahead_sync(Lookahead *l, const Sim *shown, uint64_t (*now_ms)(void),
                    uint64_t budget_ms, char *err, size_t len)
{
    if (!make_copy(l, shown, err, len)) {
        return false;
    }
    uint64_t start = now_ms();
    while (!far_enough(l, shown)) {
        sim_step(&l->ahead);
        /* Unsigned subtraction: right even if the clock wrapped round. */
        if (now_ms() - start >= budget_ms) {
            break;
        }
    }
    return true;
}

void lookahead_sync_all(Lookahead *l, const Sim *shown)
{
    char err[160];
    if (!make_copy(l, shown, err, sizeof(err))) {
        return;
    }
    while (!far_enough(l, shown)) {
        sim_step(&l->ahead);
    }
}

/*
 * What each coming thing is worth to a watcher. Not chronicle_notability's
 * weights: those rank whole lives, and there a giant counts 25, which made
 * the camera follow one growing body after another (giants come 7 to 14 a
 * season on a window-sized world, kills of hunters 40 to 55, first kills 25
 * to 35, new records under 1). Here action counts most, because it is what
 * the player sees happen: a kill or a fight beats slow growth.
 */
#define WORTH_HUNTER_KILL 25.0f /* each hunter it kills: above a first kill (21) */
#define WORTH_FIRST_KILL 15.0f  /* its first kill ever, on top of the kill */
#define WORTH_KILL 6.0f         /* each kill */
#define WORTH_RECORD 10.0f      /* becomes the oldest or the largest alive */
#define WORTH_GIANT 8.0f        /* grows to giant size */
#define WORTH_FIGHT 3.0f        /* each fight */
#define WORTH_CHILD 2.0f        /* each young */
#define WORTH_MOVE 1.0f         /* each new land settled in */
#define WORTH_WOUND 5.0f        /* each heavy wound: a fight that nearly ended it */
#define WORTH_KIN_LOST 2.0f     /* each family member that dies, up to three */
#define KIN_LOST_MOST 3.0f      /* a die-off would otherwise make every
                                 * member of a big family the best pick */

/* How many more of a count later has than now. Counts only grow, but a
 * subtraction of unsigned numbers that went the other way would wrap round to
 * about 4 billion, so the check comes first. */
static float more(uint32_t now, uint32_t later)
{
    return later > now ? (float)(later - now) : 0.0f;
}

/* The lines, in the order of LookReason (lookahead.h). */
static const char *const REASON_TEXT[LOOK_REASON_COUNT] = {
    "a quiet season",
    "about to set out for new land",
    "young on the way",
    "a fight coming",
    "about to grow giant",
    "about to be the oldest alive",
    "about to be the largest alive",
    "first kill soon",
    "more kills to come",
    "about to kill a hunter",
    "a bad wound coming",
    "about to lose family",
};

/* The kind of each reason, in the order of LookReason. */
static const int REASON_KIND[LOOK_REASON_COUNT] = {
    LOOK_KIND_QUIET,  /* quiet */
    LOOK_KIND_LIFE,   /* move */
    LOOK_KIND_FAMILY, /* young */
    LOOK_KIND_FIGHT,  /* fight */
    LOOK_KIND_BODY,   /* giant */
    LOOK_KIND_LIFE,   /* oldest */
    LOOK_KIND_BODY,   /* largest */
    LOOK_KIND_HUNT,   /* first kill */
    LOOK_KIND_HUNT,   /* more kills */
    LOOK_KIND_HUNT,   /* hunter kill */
    LOOK_KIND_FIGHT,  /* wound */
    LOOK_KIND_FAMILY, /* kin lost */
};

/* Checked by the compiler: a reason added to the enum without a text or a
 * kind would read past the end of these arrays. sizeof an array over sizeof
 * one element is its length, known at compile time. */
_Static_assert(sizeof(REASON_TEXT) / sizeof(REASON_TEXT[0]) == LOOK_REASON_COUNT,
               "a LookReason without its text");
_Static_assert(sizeof(REASON_KIND) / sizeof(REASON_KIND[0]) == LOOK_REASON_COUNT,
               "a LookReason without its kind");

int lookahead_kind(int reason)
{
    return reason >= 0 && reason < LOOK_REASON_COUNT ? REASON_KIND[reason]
                                                     : LOOK_KIND_QUIET;
}

const char *lookahead_reason_text(int reason)
{
    return reason >= 0 && reason < LOOK_REASON_COUNT ? REASON_TEXT[reason]
                                                     : REASON_TEXT[0];
}

/* Adds worth to *score, and names the line when it is the biggest part. */
static void add_part(float worth, int reason, float *score, float *biggest,
                     int *why)
{
    *score += worth;
    if (worth > *biggest) {
        *biggest = worth;
        *why = reason;
    }
}

float lookahead_score(const LifeTally *now, uint32_t age_now,
                      const LifeTally *later, uint32_t age_later, int *why)
{
    /* A flag that is set later but not now. ~ flips every bit, and on a
     * uint8_t it works on an int (integer promotion), so the result is cut
     * back to 8 bits by the cast. */
    uint8_t fresh = (uint8_t)(later->flags & ~now->flags);
    float kills = more(now->kills, later->kills);
    float score = 0.0f, biggest = 0.0f;
    *why = LOOK_QUIET;
    add_part(WORTH_MOVE * more(now->moves, later->moves), LOOK_MOVE,
             &score, &biggest, why);
    add_part(WORTH_CHILD * more(now->children, later->children), LOOK_YOUNG, &score,
             &biggest, why);
    add_part(WORTH_FIGHT * more(now->fights, later->fights), LOOK_FIGHT,
             &score, &biggest, why);
    add_part((fresh & TALLY_GIANT) ? WORTH_GIANT : 0.0f, LOOK_GIANT,
             &score, &biggest, why);
    add_part((fresh & TALLY_WAS_OLDEST) ? WORTH_RECORD : 0.0f, LOOK_OLDEST, &score,
             &biggest, why);
    add_part((fresh & TALLY_WAS_LARGEST) ? WORTH_RECORD : 0.0f, LOOK_LARGEST, &score,
             &biggest, why);
    float first = (now->kills == 0 && kills > 0.0f) ? WORTH_FIRST_KILL : 0.0f;
    add_part(WORTH_KILL * kills + first,
             first > 0.0f ? LOOK_FIRST_KILL : LOOK_MORE_KILLS, &score, &biggest,
             why);
    add_part(WORTH_HUNTER_KILL * more(now->hunter_kills, later->hunter_kills),
             LOOK_HUNTER_KILL, &score, &biggest, why);
    add_part(WORTH_WOUND * more(now->heavy_wounds, later->heavy_wounds),
             LOOK_WOUND, &score, &biggest, why);
    float lost = more(now->kin_lost, later->kin_lost);
    add_part(WORTH_KIN_LOST * (lost < KIN_LOST_MOST ? lost : KIN_LOST_MOST),
             LOOK_KIN_LOST, &score, &biggest, why);
    /* A thousandth a tick lived, as in chronicle_notability: among lives with
     * nothing coming, the one that lives through the season ranks higher. */
    return score + more(age_now, age_later) / 1000.0f;
}

/* id's tally and age in the copy, and whether it is still alive there. The
 * copy keeps every creature in the slot it had (the pool never moves a living
 * one), so the same slot is looked at first; a creature that died meanwhile
 * is found in the copy's chronicle only if its life was notable. False when
 * it is in neither: it died with nothing worth telling. */
static bool future_of(const Sim *ahead, int slot, uint32_t id,
                      const LifeTally **tally, uint32_t *age, bool *alive)
{
    const Creature *c = &ahead->pool.items[slot];
    if (c->alive && c->id == id) {
        *tally = &c->tally;
        *age = c->age;
        *alive = true;
        return true;
    }
    const NotableLife *n = chronicle_find(&ahead->chronicle, id);
    if (n == NULL) {
        return false;
    }
    *tally = &n->tally;
    *age = n->age;
    *alive = n->alive;
    return true;
}

/* The best of one kind: its slot (-1 for none yet), score and reason. */
typedef struct {
    int slot;
    float score;
    int why;
} Best;

/*
 * Which kind's best to show: the best of all, unless it is of a tired kind
 * and another kind, not tired, has something coming: a score of
 * LOOKAHEAD_OTHER_WORTH or more, one young's worth. Not a share of the best:
 * a season with a hunter killed scores 30 to 100 and one with young or a
 * fight 2 to 6, so "within a quarter of the best" let 139 of 160 picks on four
 * worlds be "about to kill a hunter". Kinds are tried in order, so the choice
 * is the same on every run. -1 when nobody was scored.
 */
static int chosen_kind(const Best best[LOOK_KIND_COUNT], unsigned tired)
{
    int top = -1;
    for (int k = 0; k < LOOK_KIND_COUNT; k++) {
        if (best[k].slot >= 0 && (top < 0 || best[k].score > best[top].score)) {
            top = k;
        }
    }
    if (top < 0 || (tired & (1u << top)) == 0) {
        return top;
    }
    int other = -1;
    for (int k = 0; k < LOOK_KIND_COUNT; k++) {
        if (best[k].slot < 0 || (tired & (1u << k)) != 0 ||
            best[k].score < LOOKAHEAD_OTHER_WORTH) {
            continue;
        }
        if (other < 0 || best[k].score > best[other].score) {
            other = k;
        }
    }
    return other >= 0 ? other : top;
}

uint32_t lookahead_pick(const Lookahead *l, const Sim *shown, unsigned tired,
                        int *reason, char *why, size_t len)
{
    why[0] = '\0';
    *reason = LOOK_QUIET;
    if (!lookahead_ready(l, shown)) {
        return LOOKAHEAD_NOBODY;
    }
    Best best[LOOK_KIND_COUNT];
    for (int k = 0; k < LOOK_KIND_COUNT; k++) {
        best[k] = (Best){.slot = -1};
    }
    for (int i = 0; i < shown->pool.capacity; i++) {
        const Creature *c = &shown->pool.items[i];
        if (!c->alive) {
            continue;
        }
        const LifeTally *later;
        uint32_t age_later;
        bool alive_later;
        if (!future_of(&l->ahead, i, c->id, &later, &age_later, &alive_later)) {
            continue;
        }
        int part;
        float score = lookahead_score(&c->tally, c->age, later, age_later, &part);
        /* A life that ends within the season is a short watch: the camera
         * would soon have to look for someone else. */
        if (!alive_later) {
            score *= 0.5f;
        }
        /* Strictly better: at equal scores the lower slot stays, so the pick
         * is the same on every run. */
        Best *b = &best[lookahead_kind(part)];
        if (b->slot < 0 || score > b->score) {
            *b = (Best){.slot = i, .score = score, .why = part};
        }
    }
    int k = chosen_kind(best, tired);
    if (k < 0) {
        return LOOKAHEAD_NOBODY;
    }
    const Creature *c = &shown->pool.items[best[k].slot];
    char name[32];
    name_from_id(c->id, name, sizeof(name));
    bool young = c->age < (uint32_t)shown->set.mate_age;
    snprintf(why, len, "%s, a %s %s: %s", name, young ? "young" : "grown",
             creature_is_hunter(c) ? "hunter" : "plant eater",
             lookahead_reason_text(best[k].why));
    *reason = best[k].why;
    return c->id;
}

void lookahead_free(Lookahead *l)
{
    if (l->has) {
        sim_free(&l->ahead);
    }
    uint64_t lead = l->lead;
    *l = (Lookahead){0};
    l->lead = lead;
}
