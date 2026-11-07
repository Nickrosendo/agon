/*
 * balance_report: runs one world and prints one row about its births and
 * deaths. A measuring tool, built by "make balance" and never part of "make
 * test". It only reads the Sim, through a pointer to const once a step is
 * done, so it cannot change the world it measures.
 *
 * Nothing in the Sim announces a death, so this keeps a shadow of each pool
 * slot: who was there at the end of the last tick, and what they had. A slot
 * whose creature has gone dead, or now holds another id, means that creature
 * died during this tick. The shadow's energy is therefore the energy just
 * before death. What killed it comes from the Pool's own count.
 */
#include <math.h> /* sqrt */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chronicle.h"
#include "climate.h"
#include "genome.h"
#include "rng.h"
#include "settings.h"
#include "sim.h"
#include "years.h"

/* A creature that had not changed cell for this many ticks counts as still. */
#define STILL_TICKS 200
/* Deaths before this tick are not counted: they are the founders' die-off.
 * Births are always counted. */
#define COUNT_DEATHS_FROM 5000

/* Younger creatures have had no time to go anywhere, and would hide how far the
 * grown ones roam. */
#define TRAVEL_MIN_AGE 300u

/* A hunter keeps being watched for this many ticks after it loses sight of
 * whoever it last saw. */
#define LOST_SIGHT_TICKS 5

/* After a relative dies nearby, how long a move to another region still counts as
 * following the loss. */
#define AFTER_LOSS_TICKS 300

typedef struct {
    bool alive;
    uint32_t id;
    int x, y;
    float energy;
    uint32_t age;
    uint8_t last_action;
    uint32_t still_ticks; /* ticks since it changed cell */
    int born_x, born_y;   /* where it was first seen */
    /* The fight this creature is in as a target, if any. */
    uint32_t hits_taken;
    bool in_fight;
    uint64_t fight_start, fight_last;
    uint32_t fight_hits;
    /* Where a hunter last saw someone, and for how many ticks it has not. */
    bool saw_someone;
    int seen_x, seen_y;
    int lost_for;
    /* Its life story as read so far: how many of its events have been looked
     * at, and the latest kin loss being watched for a move. */
    uint32_t events_seen;
    uint32_t kin_lost_at;
    bool kin_open, kin_moved;
} Shadow;

typedef struct {
    int peak;
    uint64_t extinct_at;
    bool extinct;
    uint64_t last_birth;
    bool any_birth;
    uint32_t first_born_id; /* ids below this were there at tick 0 */
    bool pool_filled;
    uint64_t deaths;
    uint32_t deaths_at_start[DEATH_COUNT]; /* the Pool's count when counting began */
    bool started_counting;
    double energy_at_death;
    double age_at_death; /* of those who grew up: most deaths are infants */
    uint64_t adult_deaths;
    uint32_t oldest_at_death;
    uint64_t still_deaths;
    uint64_t still_starving_deaths;
    uint64_t fights, fight_ticks, fight_hits;
    uint64_t chase_towards, chase_away;
    double target_kin, other_neighbour_kin;
    uint64_t target_kin_n, other_neighbour_kin_n;
    double beside_kin, anyone_kin;
    uint64_t beside_kin_n;
    uint32_t next_pick; /* steps through the slots for comparison picks */
    uint64_t still_by_action[ACT_COUNT];
    uint64_t ticks_at_plant_cap;
    uint64_t ticks_counted;
    /* Moving away after losses: kin losses watched, how many were followed by a
     * move within AFTER_LOSS_TICKS, all moves, and creature-ticks lived. */
    uint64_t kin_losses, moved_after_loss, moves, creature_ticks;
} Report;

/* The seasons' verdict (the seasons design, section 8) is judged on the whole
 * run, the founders' die-off included, so it keeps its own counts beside the
 * Report's, which start at COUNT_DEATHS_FROM. */
/* Ticks 0 to 5,999: the first ten years of 600 ticks. */
#define FIRST_YEARS_TICKS 6000u
/* A death whose cause could not be read: its slot was already reused. The
 * index after the last DEATH_ value. */
#define CAUSE_UNKNOWN DEATH_COUNT

typedef struct {
    /* The population summed after every tick, and how many ticks were summed. */
    double pop_first, pop_all;
    uint64_t ticks_first, ticks_all;
    uint64_t deaths[DEATH_COUNT + 1]; /* by cause, CAUSE_UNKNOWN last */
    uint64_t adult_deaths;
    uint64_t adult_by_cause[DEATH_COUNT + 1];
    double adult_age; /* summed over adult deaths */
    uint64_t giants_starved[SEASON_COUNT]; /* by the season they died in */
    uint64_t births[SEASON_COUNT];         /* by the season they were born in */
    uint64_t starts[SEASON_COUNT];         /* how many times each season began */
    double pop_at_start[SEASON_COUNT];     /* the population as each began, summed */
} Verdict;

static const char *const SEASON_NAMES[SEASON_COUNT] = {"spring", "summer",
                                                       "autumn", "winter"};

/* Why the creature that held this slot died, read from the last event of its
 * story: the accounting notes LIFE_DIED while it still holds the slot, and no
 * one is born into a slot freed there until the next tick's turns. Unknown
 * when the slot was taken all the same (a floor from a settings file refills
 * slots at once) or no stories are recorded. */
static int cause_of_death(const Creature *slot, uint32_t id)
{
    if (slot->alive || slot->id != id || slot->events_noted == 0) {
        return CAUSE_UNKNOWN;
    }
    const LifeEvent *last = &slot->events[(slot->events_noted - 1) % CREATURE_EVENTS];
    if (last->kind != LIFE_DIED || last->value < 0 || last->value >= DEATH_COUNT) {
        return CAUSE_UNKNOWN;
    }
    return (int)last->value;
}

/* One death for the verdict. tick is the tick the step ran, the one it died in. */
static void verdict_death(Verdict *v, const Creature *slot, const Shadow *sh,
                          const Settings *set, uint64_t tick)
{
    int cause = cause_of_death(slot, sh->id);
    v->deaths[cause]++;
    if (sh->age >= (uint32_t)set->mate_age) {
        v->adult_deaths++;
        v->adult_by_cause[cause]++;
        v->adult_age += (double)sh->age;
    }
    bool giant = cause != CAUSE_UNKNOWN && (slot->tally.flags & TALLY_GIANT);
    if (giant && cause == DEATH_HUNGER) {
        v->giants_starved[years_season_of(tick, set->ticks_per_year)]++;
    }
}

/* One death, seen after the fact from the slot's shadow. */
static void record_death(Report *rep, const Shadow *sh, const Settings *set)
{
    rep->deaths++;
    rep->energy_at_death += (double)sh->energy;
    if (sh->age >= (uint32_t)set->mate_age) {
        rep->age_at_death += (double)sh->age;
        rep->adult_deaths++;
    }
    if (sh->age > rep->oldest_at_death) {
        rep->oldest_at_death = sh->age;
    }
    if (sh->still_ticks >= STILL_TICKS) {
        rep->still_deaths++;
        /* At 0 energy a tick ago: it had been losing health to hunger. */
        if (sh->energy <= 0.0f) {
            rep->still_starving_deaths++;
        }
        if (sh->last_action < ACT_COUNT) {
            rep->still_by_action[sh->last_action]++;
        }
    }
}

static void close_fight(const Shadow *sh, Report *rep)
{
    rep->fights++;
    rep->fight_ticks += sh->fight_last - sh->fight_start + 1;
    rep->fight_hits += sh->fight_hits;
}

/* Keeps count of a target's fight. A fight ends when gang_window ticks pass with
 * no hit: the same span in which others' hits still help an attacker. */
static void watch_fight(const Sim *s, const Creature *c, Shadow *sh, Report *rep)
{
    uint64_t window = (uint64_t)s->set.gang_window;
    uint32_t new_hits = c->hits_taken - sh->hits_taken;
    if (sh->in_fight && s->tick - sh->fight_last > window) {
        close_fight(sh, rep);
        sh->in_fight = false;
    }
    if (new_hits == 0) {
        return;
    }
    if (!sh->in_fight) {
        sh->in_fight = true;
        sh->fight_start = s->tick;
        sh->fight_hits = 0;
    }
    sh->fight_last = s->tick;
    sh->fight_hits += new_hits;
}

/* The living creature with this id on one of the four cells beside (x, y), or
 * NULL. Whoever lands a hit stands there, unless it has moved since, in the same
 * tick; those few are left out rather than searched for across the pool. */
static const Creature *beside_with_id(const Sim *s, int x, int y, uint32_t id)
{
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    for (int k = 0; k < 4; k++) {
        int other = s->world.occupant[world_idx(&s->world, x + DX[k], y + DY[k])];
        if (other >= 0 && s->pool.items[other].id == id) {
            return &s->pool.items[other];
        }
    }
    return NULL;
}

/* A living creature picked without the Sim's random numbers: the next one along
 * from a position stepped by a large prime. NULL when none is found. */
static const Creature *any_creature(const Sim *s, Report *rep)
{
    uint32_t capacity = (uint32_t)s->pool.capacity;
    rep->next_pick = (rep->next_pick + 7919u) % capacity;
    for (uint32_t k = 0; k < capacity; k++) {
        const Creature *c = &s->pool.items[(rep->next_pick + k) % capacity];
        if (c->alive) {
            return c;
        }
    }
    return NULL;
}

/* For one hit by a hunter on target: how related it was to whom it chose, against
 * another neighbour it could have chosen; and, when someone else had just hit the
 * same target, how related those two were, against anyone in the world. */
static void watch_hit_kinship(const Sim *s, const Creature *target,
                              const Hit *hit, Report *rep)
{
    const Creature *by = beside_with_id(s, target->x, target->y, hit->by);
    if (by == NULL || !creature_is_hunter(by)) {
        return;
    }
    rep->target_kin += (double)genome_kinship(&by->genome, &target->genome);
    rep->target_kin_n++;
    static const int AX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    static const int AY[8] = {-1, -1, 0, 1, 1, 1, 0, -1};
    for (int k = 0; k < 8; k++) {
        int cell = world_idx(&s->world, by->x + AX[k], by->y + AY[k]);
        int other = s->world.occupant[cell];
        if (other >= 0 && &s->pool.items[other] != target) {
            rep->other_neighbour_kin +=
                (double)genome_kinship(&by->genome, &s->pool.items[other].genome);
            rep->other_neighbour_kin_n++;
            break;
        }
    }
    uint32_t kept = target->hits_taken < HITS_KEPT ? target->hits_taken : HITS_KEPT;
    for (uint32_t i = 0; i < kept; i++) {
        const Hit *h = &target->recent_hits[i];
        if (h->by == hit->by || hit->tick - h->tick > (uint32_t)s->set.gang_window) {
            continue;
        }
        const Creature *also = beside_with_id(s, target->x, target->y, h->by);
        const Creature *anyone = any_creature(s, rep);
        if (also != NULL && anyone != NULL) {
            rep->beside_kin += (double)genome_kinship(&by->genome, &also->genome);
            rep->anyone_kin += (double)genome_kinship(&by->genome, &anyone->genome);
            rep->beside_kin_n++;
        }
    }
}

/* The newest hits this creature took since the shadow last looked. */
static void watch_new_hits(const Sim *s, const Creature *c, const Shadow *sh,
                           Report *rep)
{
    uint32_t new_hits = c->hits_taken - sh->hits_taken;
    if (new_hits > HITS_KEPT) {
        new_hits = HITS_KEPT;
    }
    for (uint32_t k = 0; k < new_hits; k++) {
        uint32_t at = (c->hits_taken - 1u - k) % HITS_KEPT;
        watch_hit_kinship(s, c, &c->recent_hits[at], rep);
    }
}

/* Steps between two cells on a world that wraps, along both axes together. */
static int wrapped_distance(const World *w, int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    dx = dx < w->w - dx ? dx : w->w - dx;
    dy = dy < w->h - dy ? dy : w->h - dy;
    return dx + dy;
}

/* Fills in where a hunter now sees someone, or counts one more tick of not. Reads
 * through creature_sense, which draws no random numbers. */
static void watch_sight(const Sim *s, int idx, const Creature *c, Shadow *sh)
{
    static const int DX[4] = {0, 1, 0, -1}, DY[4] = {-1, 0, 1, 0};
    Senses in;
    creature_sense(&s->world, &s->pool, idx, &s->set, &in);
    /* The rays' headings, in RAY_ order: ahead, then left and right. */
    const int headings[RAY_COUNT] = {c->heading, (c->heading + 3) % 4,
                                     (c->heading + 1) % 4};
    int sight = creature_sight(c, &s->set);
    for (int ray = 0; ray < RAY_COUNT; ray++) {
        float near = in.v[SENSE_RAYS + ray * RAY_VALUES + RAY_NEAR];
        if (near > 0.0f) {
            /* The sense is 1 - (d - 1) / sight, undone here for d. + 0.5f rounds. */
            int d = 1 + (int)((1.0f - near) * (float)sight + 0.5f);
            sh->saw_someone = true;
            sh->seen_x = c->x + d * DX[headings[ray]];
            sh->seen_y = c->y + d * DY[headings[ray]];
            sh->lost_for = 0;
            return;
        }
    }
    sh->lost_for++;
}

/* Whether a hunter that has just lost sight of someone is walking towards where
 * they were. Half and half is what walking about at random would give. */
static void watch_chase(const Sim *s, const Creature *c, const Shadow *before,
                        Report *rep)
{
    bool moved = c->x != before->x || c->y != before->y;
    if (!moved || !before->saw_someone || before->lost_for < 1 ||
        before->lost_for > LOST_SIGHT_TICKS) {
        return;
    }
    int was = wrapped_distance(&s->world, before->x, before->y, before->seen_x,
                               before->seen_y);
    int now = wrapped_distance(&s->world, c->x, c->y, before->seen_x,
                               before->seen_y);
    if (now < was) {
        rep->chase_towards++;
    } else if (now > was) {
        rep->chase_away++;
    }
}

/* The events a creature's story gained since the last tick: kin lost near it,
 * and settling in another region. Only the newest CREATURE_EVENTS are still in
 * its ring; in one tick a creature gains far fewer. */
static void watch_story(const Sim *s, const Creature *c, Shadow *sh, Report *rep)
{
    uint32_t now = (uint32_t)s->tick; /* event ticks are the low 32 bits */
    uint32_t from = sh->events_seen;
    if (c->events_noted - from > CREATURE_EVENTS) {
        from = c->events_noted - CREATURE_EVENTS;
    }
    for (uint32_t k = from; k < c->events_noted; k++) {
        const LifeEvent *e = &c->events[k % CREATURE_EVENTS];
        bool open = sh->kin_open && now - sh->kin_lost_at <= AFTER_LOSS_TICKS;
        if (e->kind == LIFE_KIN_LOST && !open) {
            rep->kin_losses++;
            sh->kin_open = true;
            sh->kin_moved = false;
            sh->kin_lost_at = e->tick;
        } else if (e->kind == LIFE_MOVED) {
            rep->moves++;
            if (open && !sh->kin_moved) {
                rep->moved_after_loss++;
                sh->kin_moved = true;
            }
        }
    }
    sh->events_seen = c->events_noted;
    rep->creature_ticks++;
}

/* Compares every slot with its shadow, then brings the shadow up to date. */
static void watch_slots(const Sim *s, Shadow *shadow, Report *rep, Verdict *v)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        Shadow *sh = &shadow[i];
        bool same = sh->alive && c->alive && c->id == sh->id;
        bool counting = s->tick > COUNT_DEATHS_FROM;
        /* s->tick is already the next one; the death happened in the one before.
         * At tick 0 no shadow is alive yet, so the - 1 never wraps below 0. */
        if (sh->alive && !same) {
            verdict_death(v, c, sh, &s->set, s->tick - 1);
        }
        if (sh->alive && !same && counting) {
            record_death(rep, sh, &s->set);
            if (sh->in_fight) {
                close_fight(sh, rep);
            }
        }
        if (!c->alive) {
            sh->alive = false;
            continue;
        }
        uint32_t still = 0;
        if (same && c->x == sh->x && c->y == sh->y) {
            still = sh->still_ticks + 1;
        }
        /* A copy to carry on from, or for a new creature an empty one. A struct
         * can be assigned whole; its fields are copied, not shared. */
        Shadow next = same ? *sh : (Shadow){0};
        if (!same) {
            next.born_x = c->x;
            next.born_y = c->y;
        }
        if (counting) {
            watch_story(s, c, &next, rep);
            watch_new_hits(s, c, &next, rep);
            watch_fight(s, c, &next, rep);
            if (creature_is_hunter(c)) {
                watch_chase(s, c, &next, rep);
                watch_sight(s, i, c, &next);
            }
        }
        next.alive = true;
        next.id = c->id;
        next.x = c->x;
        next.y = c->y;
        next.energy = c->energy;
        next.age = c->age;
        next.last_action = c->current.kind;
        next.still_ticks = still;
        next.hits_taken = c->hits_taken;
        *sh = next;
    }
}

/* The shorter way between two coordinates on a world that wraps around. */
static int wrapped_gap(int a, int b, int size)
{
    int gap = abs(a - b);
    return gap < size - gap ? gap : size - gap;
}

/* How far the creatures alive now, of TRAVEL_MIN_AGE ticks or more, stand from
 * where they were born: straight-line cells, the short way round. How far they
 * roam, not how far they walked: pacing back and forth ends up near the start. */
static void print_travel(const Sim *s, const Shadow *shadow)
{
    double total = 0.0;
    int adults = 0;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (!c->alive || c->age < TRAVEL_MIN_AGE) {
            continue;
        }
        double dx = wrapped_gap(c->x, shadow[i].born_x, s->world.w);
        double dy = wrapped_gap(c->y, shadow[i].born_y, s->world.h);
        total += sqrt(dx * dx + dy * dy);
        adults++;
    }
    printf("     travel: adults stand %.1f cells from birth (%d of 300+ ticks)\n",
           adults == 0 ? 0.0 : total / adults, adults);
}

static int meat_eaters(const Sim *s)
{
    int n = 0;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (c->alive && c->level >= LEVEL_MEAT) {
            n++;
        }
    }
    return n;
}

/* A share as a whole percentage; 0 when there is nothing to share out. The
 * conversion to double happens before the division: two integers divided
 * would lose the fraction first. */
static int percent(uint64_t part, uint64_t whole)
{
    return whole == 0 ? 0 : (int)(100.0 * (double)part / (double)whole + 0.5);
}

/* The largest body alive and the mean size, for watching growth. */
static void sizes(const Sim *s, float *largest, float *mean)
{
    double total = 0.0;
    *largest = 0.0f;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (c->alive) {
            float size = creature_size(c, &s->set);
            total += (double)size;
            *largest = size > *largest ? size : *largest;
        }
    }
    *mean = s->pool.count > 0 ? (float)(total / (double)s->pool.count) : 0.0f;
}

static void print_header(void)
{
    printf("seed  final   peak extinct births lastbirth meat  Emean adultA ageX hungr "
           "woun  old still  rest left right move bite  mate atck  capt full  szmax szmean\n");
}

/* What no pass rule depends on: fights, chasing and who fights alongside whom. */
/* How the plants stand at the end: how much of the soil holds one, how many
 * lean to fruit and to leaves, how much ripe fruit hangs, and the genes' means. */
static void print_plants(const Sim *s)
{
    const World *w = &s->world;
    int soil = 0, planted = 0, fruit_plants = 0, leaf_plants = 0;
    double fruit = 0.0, need = 0.0, energy = 0.0;
    for (int i = 0; i < w->w * w->h; i++) {
        soil += w->terrain[i] == TERRAIN_SOIL;
        if (w->veg[i] == 0) {
            continue;
        }
        planted++;
        const Plant *p = &w->plant[i];
        if (p->fruit_share > 0.5f) {
            fruit_plants++;
        } else {
            leaf_plants++;
        }
        fruit += (double)p->fruit;
        need += (double)p->water_need;
        energy += (double)p->energy;
    }
    double n = planted > 0 ? (double)planted : 1.0;
    printf("     plants: %d cells (%d%% of soil), %d lean to fruit, %d to leaves, "
           "%.0f ripe fruit, mean water need %.2f, mean energy %.1f\n",
           planted, percent((uint64_t)planted, (uint64_t)soil), fruit_plants,
           leaf_plants, fruit, need / n, energy / n);
}

/* The body traits compared across regions: the genes behind what regions reward,
 * with colour apart, because a mean of colours needs the wheel. */
enum { TRAIT_SIZE, TRAIT_SPEED, TRAIT_LEGS, TRAIT_SWIM, TRAIT_LEAF, TRAIT_FRUIT,
       TRAIT_MEAT, TRAIT_MATCH, TRAITS };
static const char *const TRAIT_NAMES[TRAITS] = {
    "size", "speed", "legs", "swim", "leaf", "fruit", "meat", "match",
};
static const char *const REGION_NAMES[REGION_COUNT] = {
    "cold", "hot/dry", "temperate", "wet", "shallow",
};
/* A region with fewer creatures than this has means too noisy to compare. */
#define REGION_MIN_COUNT 30
#define SHUFFLES 50

/* The value of a trait for one creature. "match" is 1 when its colour hides it on
 * the ground of the region named, whichever region it stands in. */
static double trait_value(const Creature *c, int trait, int region)
{
    const float *g = c->genome.g;
    switch (trait) {
    case TRAIT_SIZE: return (double)g[GENE_SIZE];
    case TRAIT_SPEED: return (double)g[GENE_SPEED];
    case TRAIT_LEGS: return (double)g[GENE_LEGS];
    case TRAIT_SWIM: return (double)g[GENE_SWIM];
    case TRAIT_LEAF: return (double)g[GENE_LEAF];
    case TRAIT_FRUIT: return (double)g[GENE_FRUIT];
    case TRAIT_MEAT: return (double)g[GENE_MEAT];
    default: {
        float d = g[GENE_HUE] - climate_region_hue(region);
        d = d < 0.0f ? -d : d;
        d = d > 0.5f ? 1.0f - d : d;
        return d < 0.15f ? 1.0f : 0.0f;
    }
    }
}

/* The largest gap between two regions' means, given each creature's region. */
static double trait_spread(const Sim *s, const int *slots, const int *regions, int n,
                           int trait)
{
    double sum[REGION_COUNT] = {0.0};
    int count[REGION_COUNT] = {0};
    for (int k = 0; k < n; k++) {
        sum[regions[k]] += trait_value(&s->pool.items[slots[k]], trait, regions[k]);
        count[regions[k]]++;
    }
    double low = 2.0, high = -1.0;
    for (int r = 0; r < REGION_COUNT; r++) {
        if (count[r] < REGION_MIN_COUNT) {
            continue;
        }
        double mean = sum[r] / count[r];
        low = mean < low ? mean : low;
        high = mean > high ? mean : high;
    }
    return high > low ? high - low : 0.0;
}

/*
 * Do regions hold different bodies? Each trait's spread between regions, against
 * the same creatures dealt out to regions at random, keeping each region's head
 * count. A trait counts as different only when it beats every shuffle. The
 * report's own generator: drawing from the Sim's would change the world measured.
 */
static void print_regions(const Sim *s)
{
    int *slots = malloc((size_t)s->pool.capacity * sizeof(*slots));
    int *regions = malloc((size_t)s->pool.capacity * sizeof(*regions));
    if (slots == NULL || regions == NULL) {
        free(slots);
        free(regions);
        return;
    }
    int n = 0;
    int count[REGION_COUNT] = {0};
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (!c->alive) {
            continue;
        }
        int region = climate_region(&s->world, world_idx(&s->world, c->x, c->y), &s->set);
        if (region == REGION_NONE) {
            continue; /* not an index */
        }
        slots[n] = i;
        regions[n] = region;
        count[region]++;
        n++;
    }
    printf("     regions:");
    for (int r = 0; r < REGION_COUNT; r++) {
        printf(" %s %d", REGION_NAMES[r], count[r]);
    }
    printf("\n     trait spread (real / highest shuffled):");
    Rng rng;
    rng_seed(&rng, 12345);
    int beat = 0;
    for (int t = 0; t < TRAITS; t++) {
        double real = trait_spread(s, slots, regions, n, t);
        double top = 0.0;
        for (int k = 0; k < SHUFFLES; k++) {
            for (int i = n - 1; i > 0; i--) {
                int j = (int)rng_below(&rng, (uint32_t)i + 1u);
                int tmp = regions[i];
                regions[i] = regions[j];
                regions[j] = tmp;
            }
            double spread = trait_spread(s, slots, regions, n, t);
            top = spread > top ? spread : top;
        }
        /* The shuffles scattered who stands where; the real table comes next
         * time from a fresh look, so put each creature back. */
        for (int i = 0; i < n; i++) {
            const Creature *c = &s->pool.items[slots[i]];
            regions[i] = climate_region(&s->world, world_idx(&s->world, c->x, c->y),
                                        &s->set);
        }
        printf(" %s %.2f/%.2f%s", TRAIT_NAMES[t], real, top, real > top ? "*" : "");
        beat += real > top;
    }
    printf("\n     traits beating chance: %d of %d\n", beat, TRAITS);
    free(slots);
    free(regions);
}

/* The root of slot x's group, shortening the path as it climbs. */
static int group_root(int *parent, int x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

/*
 * Species, as far as the numbers can say: creatures joined when their bodies pass
 * the breeding barrier, directly or through a chain of others. Two groups with no
 * such chain cannot exchange a gene through anyone alive. A strict test: one
 * in-between creature joins two groups into one. Only groups of 10 or more count.
 */
static void print_species(const Sim *s)
{
    int *slots = malloc((size_t)s->pool.capacity * sizeof(*slots));
    int *parent = malloc((size_t)s->pool.capacity * sizeof(*parent));
    int *size = calloc((size_t)s->pool.capacity, sizeof(*size));
    if (slots == NULL || parent == NULL || size == NULL) {
        free(slots);
        free(parent);
        free(size);
        return;
    }
    int n = 0;
    for (int i = 0; i < s->pool.capacity; i++) {
        if (s->pool.items[i].alive) {
            slots[n] = i;
            parent[n] = n;
            n++;
        }
    }
    uint64_t pairs = 0, barred = 0;
    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
            float like = genome_body_likeness(&s->pool.items[slots[a]].genome,
                                              &s->pool.items[slots[b]].genome);
            pairs++;
            if (like >= s->set.mate_barrier) {
                parent[group_root(parent, a)] = group_root(parent, b);
            } else {
                barred++;
            }
        }
    }
    for (int a = 0; a < n; a++) {
        size[group_root(parent, a)]++;
    }
    int species = 0, largest = 0;
    for (int a = 0; a < n; a++) {
        species += size[a] >= 10;
        largest = size[a] > largest ? size[a] : largest;
    }
    printf("     species: %d groups of 10+, largest %d of %d; pairs barred from mating %d%%\n",
           species, largest, n, percent(barred, pairs));
    free(slots);
    free(parent);
    free(size);
}

/* Giants, from the life stories, and whether losing kin is followed by moving
 * away more often than moving happens anyway. */
static void print_giants_and_losses(const Sim *s, const Report *rep)
{
    int alive = 0;
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        alive += c->alive && (c->tally.flags & TALLY_GIANT);
    }
    /* By chance: the share of AFTER_LOSS_TICKS-long stretches holding at least
     * one move, if moves came at the overall rate, 1 - e^(-rate * ticks). */
    double rate = rep->creature_ticks == 0
                      ? 0.0
                      : (double)rep->moves / (double)rep->creature_ticks;
    double chance = 1.0 - exp(-rate * AFTER_LOSS_TICKS);
    printf("     giants: %u grew to giant size (%.0f+), %d alive at the end | "
           "kin lost %llu times, a move within %d ticks after %d%%, by chance %.0f%%\n",
           (unsigned)s->chronicle.giants, (double)s->set.giant_size, alive,
           (unsigned long long)rep->kin_losses, AFTER_LOSS_TICKS,
           percent(rep->moved_after_loss, rep->kin_losses), 100.0 * chance);
}

static void print_behaviour(const Sim *s, const Report *rep)
{
    double length = rep->fights == 0
                        ? 0.0
                        : (double)rep->fight_ticks / (double)rep->fights;
    double hits = rep->fights == 0
                      ? 0.0
                      : (double)rep->fight_hits / (double)rep->fights;
    double target = rep->target_kin_n == 0
                        ? 0.0
                        : rep->target_kin / (double)rep->target_kin_n;
    double other = rep->other_neighbour_kin_n == 0
                       ? 0.0
                       : rep->other_neighbour_kin /
                             (double)rep->other_neighbour_kin_n;
    double beside = rep->beside_kin_n == 0
                        ? 0.0
                        : rep->beside_kin / (double)rep->beside_kin_n;
    double anyone = rep->beside_kin_n == 0
                        ? 0.0
                        : rep->anyone_kin / (double)rep->beside_kin_n;
    printf("  seed %llu: still+starving %d%% | fights %llu, length %.1f ticks, "
           "%.1f hits | chasing towards %d%% of %llu moves | kin of target %.2f, "
           "other neighbour %.2f (%llu) | kin of co-attacker %.2f, anyone %.2f "
           "(%llu)\n",
           (unsigned long long)s->seed,
           percent(rep->still_starving_deaths, rep->deaths),
           (unsigned long long)rep->fights, length, hits,
           percent(rep->chase_towards, rep->chase_towards + rep->chase_away),
           (unsigned long long)(rep->chase_towards + rep->chase_away), target,
           other, (unsigned long long)rep->other_neighbour_kin_n, beside, anyone,
           (unsigned long long)rep->beside_kin_n);
}

static void print_row(const Sim *s, const Shadow *shadow, const Report *rep)
{
    char extinct[32] = "-";
    if (rep->extinct) {
        snprintf(extinct, sizeof(extinct), "%llu",
                 (unsigned long long)rep->extinct_at);
    }
    char last[32] = "-";
    if (rep->any_birth) {
        snprintf(last, sizeof(last), "%llu",
                 (unsigned long long)rep->last_birth);
    }
    double mean = rep->deaths == 0 ? 0.0
                                   : rep->energy_at_death / (double)rep->deaths;
    double mean_age = rep->adult_deaths == 0
                          ? 0.0
                          : rep->age_at_death / (double)rep->adult_deaths;
    uint32_t births = s->pool.next_id - rep->first_born_id;
    printf("%4llu %6d %6d %7s %6u %9s %4d %6.1f %6.0f %4u %4d%% %3d%% %3d%% %4d%%",
           (unsigned long long)s->seed, s->pool.count, rep->peak, extinct,
           (unsigned)births, last, meat_eaters(s), mean, mean_age,
           (unsigned)rep->oldest_at_death,
           percent(s->pool.deaths[DEATH_HUNGER] - rep->deaths_at_start[DEATH_HUNGER],
                   rep->deaths),
           percent(s->pool.deaths[DEATH_WOUNDS] - rep->deaths_at_start[DEATH_WOUNDS],
                   rep->deaths),
           percent(s->pool.deaths[DEATH_AGE] - rep->deaths_at_start[DEATH_AGE],
                   rep->deaths),
           percent(rep->still_deaths, rep->deaths));
    /* The last action of the still ones, as shares of them, in ACT_ order. */
    for (int a = 0; a < ACT_COUNT; a++) {
        printf(" %4d%%", percent(rep->still_by_action[a], rep->still_deaths));
    }
    float largest, mean_size;
    sizes(s, &largest, &mean_size);
    printf(" %4d%% %4s %6.1f %6.2f\n",
           percent(rep->ticks_at_plant_cap, rep->ticks_counted),
           rep->pool_filled ? "YES" : "no", (double)largest, (double)mean_size);
    print_behaviour(s, rep);
    print_plants(s);
    print_travel(s, shadow);
    print_regions(s);
    print_species(s);
    print_giants_and_losses(s, rep);
}

/* In DeathCause order, CAUSE_UNKNOWN last. */
static const char *const CAUSE_NAMES[DEATH_COUNT + 1] = {"wounds", "hunger", "age",
                                                         "unknown"};

/* A sum divided by a count, 0 when the count is 0. */
static double mean_of(double sum, uint64_t n)
{
    return n == 0 ? 0.0 : sum / (double)n;
}

/* The commonest known cause among adult deaths; "none" with no adult deaths. A
 * tie goes to the earlier cause. */
static const char *top_adult_cause(const Verdict *v)
{
    int top = -1;
    for (int c = 0; c < DEATH_COUNT; c++) {
        if (v->adult_by_cause[c] > 0 &&
            (top < 0 || v->adult_by_cause[c] > v->adult_by_cause[top])) {
            top = c;
        }
    }
    return top < 0 ? "none" : CAUSE_NAMES[top];
}

/* What is judged at the end: plants by what they offer, creatures that eat
 * below meat level, and creatures that have killed. */
typedef struct {
    int fruit, leaf, eaters, killers;
} EndCounts;

static EndCounts end_counts(const Sim *s)
{
    EndCounts n = {0};
    const World *w = &s->world;
    for (int i = 0; i < w->w * w->h; i++) {
        if (w->veg[i] == 0) {
            continue;
        }
        if (plant_offers_fruit(&w->plant[i])) {
            n.fruit++;
        } else {
            n.leaf++;
        }
    }
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        if (!c->alive) {
            continue;
        }
        n.eaters += c->level < LEVEL_MEAT;
        n.killers += c->tally.kills > 0;
    }
    return n;
}

/* The lines reported beside the verdict and never judged: deaths by cause over
 * the whole run, giants that starved and when, births by season, and the
 * population as each season begins, averaged over the years. */
static void print_seasons(const Verdict *v)
{
    printf("     deaths (whole run):");
    for (int c = 0; c <= DEATH_COUNT; c++) {
        printf(" %s %llu", CAUSE_NAMES[c], (unsigned long long)v->deaths[c]);
    }
    printf(" | adults:");
    for (int c = 0; c <= DEATH_COUNT; c++) {
        printf(" %s %llu", CAUSE_NAMES[c], (unsigned long long)v->adult_by_cause[c]);
    }
    printf("\n     giants starved:");
    for (int k = 0; k < SEASON_COUNT; k++) {
        printf(" %s %llu", SEASON_NAMES[k], (unsigned long long)v->giants_starved[k]);
    }
    printf("\n     births per season:");
    for (int k = 0; k < SEASON_COUNT; k++) {
        printf(" %s %.1f", SEASON_NAMES[k],
               mean_of((double)v->births[k], v->starts[k]));
    }
    printf("\n     population as a season begins:");
    for (int k = 0; k < SEASON_COUNT; k++) {
        printf(" %s %.0f", SEASON_NAMES[k], mean_of(v->pop_at_start[k], v->starts[k]));
    }
    printf("\n");
}

/* The verdict's numbers for one world on one line, for tests/seasons_verdict.sh:
 * SUMMARY seed alive mean10y meanall fruit leaf eaters killers adult_life
 * hunger_adult top_adult, then the adult deaths and their split by cause
 * (wounds hunger age), which the script needs to weigh worlds against each
 * other. Adult deaths of unknown cause are in the total but in no split. */
static void print_summary(const Sim *s, const Verdict *v)
{
    EndCounts n = end_counts(s);
    printf("SUMMARY %llu %d %.1f %.1f %d %d %d %d %.0f %.3f %s %llu %llu %llu %llu\n",
           (unsigned long long)s->seed, s->pool.count > 0 ? 1 : 0,
           mean_of(v->pop_first, v->ticks_first), mean_of(v->pop_all, v->ticks_all),
           n.fruit, n.leaf, n.eaters, n.killers,
           mean_of(v->adult_age, v->adult_deaths),
           mean_of((double)v->adult_by_cause[DEATH_HUNGER], v->adult_deaths),
           top_adult_cause(v), (unsigned long long)v->adult_deaths,
           (unsigned long long)v->adult_by_cause[DEATH_WOUNDS],
           (unsigned long long)v->adult_by_cause[DEATH_HUNGER],
           (unsigned long long)v->adult_by_cause[DEATH_AGE]);
}

/* Runs the world. The Sim is changed only by sim_step. */
/* After a step: the population summed, births by season, and the population
 * as a season begins. s->tick is the tick about to run next. */
static void verdict_tick(const Sim *s, Verdict *v, uint32_t births)
{
    int tpy = s->set.ticks_per_year;
    v->pop_all += s->pool.count;
    v->ticks_all++;
    if (s->tick <= FIRST_YEARS_TICKS) {
        v->pop_first += s->pool.count;
        v->ticks_first++;
    }
    v->births[years_season_of(s->tick - 1, tpy)] += births;
    int season = years_season_of(s->tick, tpy);
    if (years_season_starts(s->tick, tpy, season)) {
        v->starts[season]++;
        v->pop_at_start[season] += s->pool.count;
    }
}

static void run(Sim *s, long ticks, Shadow *shadow, Report *rep, Verdict *v)
{
    uint32_t seen_id = s->pool.next_id;
    watch_slots(s, shadow, rep, v);
    rep->peak = s->pool.count;
    /* Tick 0 is the first of a spring. */
    v->starts[0]++;
    v->pop_at_start[0] += s->pool.count;
    for (long t = 0; t < ticks; t++) {
        /* Before the step that can first count a death, to match watch_slots. */
        if (!rep->started_counting && s->tick >= COUNT_DEATHS_FROM) {
            rep->started_counting = true;
            memcpy(rep->deaths_at_start, s->pool.deaths, sizeof(s->pool.deaths));
        }
        sim_step(s);
        watch_slots(s, shadow, rep, v);
        /* Unsigned subtraction: right even if the ids wrapped past 2^32. */
        verdict_tick(s, v, s->pool.next_id - seen_id);
        if (s->pool.next_id != seen_id) {
            seen_id = s->pool.next_id;
            rep->last_birth = s->tick;
            rep->any_birth = true;
        }
        if (s->pool.count > rep->peak) {
            rep->peak = s->pool.count;
        }
        if (s->pool.count >= s->pool.capacity) {
            rep->pool_filled = true;
        }
        if (s->tick > COUNT_DEATHS_FROM) {
            rep->ticks_counted++;
            if (s->set.max_plant_eaters > 0 &&
                s->pool.plant_eaters >= s->set.max_plant_eaters) {
                rep->ticks_at_plant_cap++;
            }
        }
        if (s->pool.count == 0) {
            rep->extinct = true;
            rep->extinct_at = s->tick;
            return; /* nothing more can happen in this world */
        }
    }
}

static int usage(void)
{
    fprintf(stderr, "usage: balance_report --seed N --ticks N [--size WxH] "
                    "[--config PATH] [--header] [--summary]\n");
    return 2;
}

int main(int argc, char **argv)
{
    long seed = 1;
    long ticks = 20000;
    int width = 256;
    int height = 144;
    const char *config = NULL;
    bool summary = false;
    for (int i = 1; i < argc; i++) {
        /* Every option here takes a value in the next argument, except
         * --header, which prints the column names and nothing else. */
        if (strcmp(argv[i], "--header") == 0) {
            print_header();
            return 0;
        }
        if (strcmp(argv[i], "--summary") == 0) {
            summary = true;
            continue;
        }
        if (i + 1 >= argc) {
            return usage();
        }
        const char *value = argv[++i];
        if (strcmp(argv[i - 1], "--seed") == 0) {
            seed = strtol(value, NULL, 10);
        } else if (strcmp(argv[i - 1], "--ticks") == 0) {
            ticks = strtol(value, NULL, 10);
        } else if (strcmp(argv[i - 1], "--size") == 0) {
            if (sscanf(value, "%dx%d", &width, &height) != 2) {
                return usage();
            }
        } else if (strcmp(argv[i - 1], "--config") == 0) {
            config = value;
        } else {
            return usage();
        }
    }

    Settings set = settings_default();
    char err[512];
    if (config != NULL && !settings_load(&set, config, err, sizeof(err))) {
        fprintf(stderr, "balance_report: %s\n", err);
        return 2;
    }
    /* A Sim is too large to want on the stack, and static storage lasts for
     * the whole program. */
    static Sim sim;
    if (!sim_init(&sim, &set, (uint64_t)seed, width, height)) {
        fprintf(stderr, "balance_report: could not build the world\n");
        return 1;
    }
    /* calloc: zeroed memory, so every shadow starts as "nobody here". */
    Shadow *shadow = calloc((size_t)sim.pool.capacity, sizeof(*shadow));
    if (shadow == NULL) {
        sim_free(&sim);
        return 1;
    }
    Report rep = {.first_born_id = sim.pool.next_id};
    Verdict verdict = {0};
    run(&sim, ticks, shadow, &rep, &verdict);
    print_row(&sim, shadow, &rep);
    print_seasons(&verdict);
    if (summary) {
        print_summary(&sim, &verdict);
    }
    free(shadow);
    sim_free(&sim);
    return 0;
}
