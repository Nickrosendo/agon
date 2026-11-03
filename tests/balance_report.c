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
 * before death.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "settings.h"
#include "sim.h"

/* Below this energy at the end of its last full tick, a creature counts as
 * starved; above it, as killed by something else. Rough: the same split the
 * design's table used. */
#define STARVED_BELOW 3.0f
/* A creature that had not changed cell for this many ticks counts as still. */
#define STILL_TICKS 200
/* Deaths before this tick are not counted: they are the founders' die-off.
 * Births are always counted. */
#define COUNT_DEATHS_FROM 5000

typedef struct {
    bool alive;
    uint32_t id;
    int x, y;
    float energy;
    uint32_t age;
    uint8_t last_action;
    uint32_t still_ticks; /* ticks since it changed cell */
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
    uint64_t starved, killed, old_age;
    double energy_at_death;
    double age_at_death; /* of those who grew up: most deaths are infants */
    uint64_t adult_deaths;
    uint32_t oldest_at_death;
    uint64_t still_deaths;
    uint64_t still_by_action[ACT_COUNT];
    uint64_t ticks_at_plant_cap;
    uint64_t ticks_counted;
} Report;

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
    /* The age check comes first: that death has nothing to do with energy.
     * sh->age is the age a tick ago, and upkeep adds one before comparing. */
    if (set->max_age > 0 && sh->age + 1 >= (uint32_t)set->max_age) {
        rep->old_age++;
    } else if (sh->energy < STARVED_BELOW) {
        rep->starved++;
    } else {
        rep->killed++;
    }
    if (sh->still_ticks >= STILL_TICKS) {
        rep->still_deaths++;
        if (sh->last_action < ACT_COUNT) {
            rep->still_by_action[sh->last_action]++;
        }
    }
}

/* Compares every slot with its shadow, then brings the shadow up to date. */
static void watch_slots(const Sim *s, Shadow *shadow, Report *rep)
{
    for (int i = 0; i < s->pool.capacity; i++) {
        const Creature *c = &s->pool.items[i];
        Shadow *sh = &shadow[i];
        bool same = sh->alive && c->alive && c->id == sh->id;
        if (sh->alive && !same && s->tick > COUNT_DEATHS_FROM) {
            record_death(rep, sh, &s->set);
        }
        if (!c->alive) {
            sh->alive = false;
            continue;
        }
        uint32_t still = 0;
        if (same && c->x == sh->x && c->y == sh->y) {
            still = sh->still_ticks + 1;
        }
        *sh = (Shadow){
            .alive = true,
            .id = c->id,
            .x = c->x,
            .y = c->y,
            .energy = c->energy,
            .age = c->age,
            .last_action = c->current.kind,
            .still_ticks = still,
        };
    }
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

static void print_header(void)
{
    printf("seed  final   peak extinct births lastbirth meat  Emean adultA ageX starv "
           "kill  old still  rest left right move bite  mate  capt full\n");
}

static void print_row(const Sim *s, const Report *rep)
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
           percent(rep->starved, rep->deaths), percent(rep->killed, rep->deaths),
           percent(rep->old_age, rep->deaths),
           percent(rep->still_deaths, rep->deaths));
    /* The last action of the still ones, as shares of them, in ACT_ order. */
    for (int a = 0; a < ACT_COUNT; a++) {
        printf(" %4d%%", percent(rep->still_by_action[a], rep->still_deaths));
    }
    printf(" %4d%% %4s\n", percent(rep->ticks_at_plant_cap, rep->ticks_counted),
           rep->pool_filled ? "YES" : "no");
}

/* Runs the world. The Sim is changed only by sim_step. */
static void run(Sim *s, long ticks, Shadow *shadow, Report *rep)
{
    uint32_t seen_id = s->pool.next_id;
    watch_slots(s, shadow, rep);
    rep->peak = s->pool.count;
    for (long t = 0; t < ticks; t++) {
        sim_step(s);
        watch_slots(s, shadow, rep);
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
                    "[--config PATH] [--header]\n");
    return 2;
}

int main(int argc, char **argv)
{
    long seed = 1;
    long ticks = 20000;
    int width = 256;
    int height = 144;
    const char *config = NULL;
    for (int i = 1; i < argc; i++) {
        /* Every option here takes a value in the next argument, except
         * --header, which prints the column names and nothing else. */
        if (strcmp(argv[i], "--header") == 0) {
            print_header();
            return 0;
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
    run(&sim, ticks, shadow, &rep);
    print_row(&sim, &rep);
    free(shadow);
    sim_free(&sim);
    return 0;
}
