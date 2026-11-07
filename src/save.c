#include "save.h"

#include <errno.h>
#include <math.h>   /* isfinite */
#include <stdint.h>
#include <stdio.h>
#include <string.h> /* memcpy, memcmp, memset, strlen, strerror */

#ifdef _WIN32
/* Asks the huge windows.h for its core only. Must come before the include:
 * a header is pasted text, and this changes what the text says. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h> /* MoveFileExA */
#endif

#include "climate.h" /* REGION_NONE, REGION_COUNT */
#include "options.h" /* OPTIONS_MAX_SIDE */

/*
 * The file, in order. Whole numbers are unsigned or two's complement,
 * lowest byte first; floats are their 32 IEEE 754 bits, the same way.
 *
 *   "AGONSAVE", then u32 version, number of settings, genome length,
 *   history length (a file from a different build is refused here);
 *   u32 width, height; u64 seed, tick; u64 x 4 random generator;
 *   every setting, in the table's order;
 *   the world's layers;
 *   three histories;
 *   the records: i32 peak, u32 oldest id, u32 oldest age, u32 largest id,
 *   float largest size, u64 ended_at;
 *   i32 free_head, u32 next_id, u32 births_refused, u32 deaths per cause;
 *   then every creature slot, a living one ending with its life story;
 *   then the chronicle's CHRONICLE_NOTABLE entries.
 *
 * Change anything here and SAVE_VERSION goes up by one.
 */
#define SAVE_MAGIC "AGONSAVE"
#define SAVE_MAGIC_LEN 8
#define SAVE_VERSION 10u

/* No value in a healthy world comes near this. Bounding them keeps sums
 * from reaching infinity, and float-to-int conversions defined. */
#define FLOAT_LIMIT 1e9f
/* More than any count a history can hold (cells, or creatures), and small
 * enough that the panel's graph arithmetic stays well inside an int. */
#define HISTORY_VALUE_MAX (1 << 25)

/* ---- writing ---- */

/*
 * Every write can fail (a full disk, say), and checking each of a thousand
 * calls would bury the format. So a sticky flag: the first failure clears
 * ok, later writes do nothing, and the caller checks once at the end.
 * Nothing is lost, as a failed write leaves nothing to rescue anyway.
 */
typedef struct {
    FILE *f;
    bool ok;
} Writer;

static void put_bytes(Writer *w, const void *bytes, size_t n)
{
    /* fwrite returns how many items it wrote: anything short is a failure. */
    if (w->ok && fwrite(bytes, 1, n, w->f) != n) {
        w->ok = false;
    }
}

/*
 * Lowest byte first, spelled out with shifts. This gives the same four
 * bytes on any machine; fwrite(&v, 4, 1, f) would write them in whatever
 * order this machine keeps them in memory.
 */
static void put_u32(Writer *w, uint32_t v)
{
    unsigned char b[4] = {
        (unsigned char)(v & 0xFFu),
        (unsigned char)((v >> 8) & 0xFFu),
        (unsigned char)((v >> 16) & 0xFFu),
        (unsigned char)((v >> 24) & 0xFFu),
    };
    put_bytes(w, b, sizeof(b));
}

static void put_u64(Writer *w, uint64_t v)
{
    put_u32(w, (uint32_t)(v & 0xFFFFFFFFu));
    put_u32(w, (uint32_t)(v >> 32));
}

/* Converting a negative int to uint32_t is defined: it wraps modulo 2^32,
 * and get_i32 undoes it. */
static void put_i32(Writer *w, int v)
{
    put_u32(w, (uint32_t)v);
}

static void put_u16(Writer *w, uint16_t v)
{
    unsigned char b[2] = {(unsigned char)(v & 0xFFu),
                          (unsigned char)(v >> 8)};
    put_bytes(w, b, sizeof(b));
}

/*
 * The float's bits, as a number. memcpy is the legal way to look at one
 * type's bytes as another's. (uint32_t)v would convert the value, turning
 * 2.5 into 2; a pointer cast, *(uint32_t *)&v, breaks C's rule that an
 * object is read through its own type, and the optimiser may rely on it.
 */
static void put_float(Writer *w, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, sizeof(bits));
    put_u32(w, bits);
}

static void put_settings(Writer *w, const Settings *set)
{
    Settings copy = *set; /* the table's accessors take a non-const pointer */
    for (int i = 0; i < settings_count(); i++) {
        const SettingInfo *info = settings_info(i);
        if (info->type == SETTING_INT) {
            put_i32(w, *settings_int(&copy, info));
        } else if (info->type == SETTING_FLOAT) {
            put_float(w, *settings_float(&copy, info));
        } else {
            /* A fixed-width field. What follows the '\0' may be leftovers
             * from an earlier longer name: zero them, so the same world
             * always gives the same file. */
            char name[BRAIN_NAME_MAX] = {0};
            const char *text = settings_text(&copy, info);
            memcpy(name, text, strlen(text));
            put_bytes(w, name, sizeof(name));
        }
    }
}

static void put_floats(Writer *w, const float *v, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        put_float(w, v[i]);
    }
}

/*
 * occupant is not written: every creature says where it stands, so the
 * layer is rebuilt on loading. A file then cannot hold an occupant index
 * pointing outside the pool. veg_next and scent_next are not written
 * either: each tick overwrites them before reading them.
 */
static void put_plant(Writer *w, const Plant *p)
{
    put_float(w, p->energy);
    put_float(w, p->fruit);
    put_float(w, p->fruit_share);
    put_float(w, p->water_need);
    put_float(w, p->temp_pref);
    put_float(w, p->gut_time);
    put_float(w, p->size);
}

static void put_world(Writer *w, const World *world)
{
    size_t n = (size_t)world->w * (size_t)world->h;
    put_bytes(w, world->terrain, n);
    put_floats(w, world->fertility, n);
    put_bytes(w, world->veg, n);
    for (size_t i = 0; i < n; i++) {
        put_u16(w, world->veg_age[i]);
    }
    put_bytes(w, world->veg_wither, n);
    for (size_t i = 0; i < n; i++) {
        put_plant(w, &world->plant[i]);
    }
    put_floats(w, world->carrion, n);
    put_floats(w, world->carrion_level, n);
    put_floats(w, world->scent, n);
    put_floats(w, world->water, n);
    put_floats(w, world->temperature, n);
    put_floats(w, world->rain, n);
    put_floats(w, world->snow, n);
    put_u64(w, world->rain_seed);
}

static void put_history(Writer *w, const History *h)
{
    for (int i = 0; i < HISTORY; i++) {
        put_i32(w, h->values[i]);
    }
    put_i32(w, h->pos);
    put_i32(w, h->len);
}

/* The struct's fields one at a time: writing the struct whole would
 * include its padding, which holds whatever was in memory. */
static void put_records(Writer *w, const WorldRecords *r)
{
    put_i32(w, r->peak_population);
    put_u32(w, r->oldest_id);
    put_u32(w, r->oldest_age);
    put_u32(w, r->largest_id);
    put_float(w, r->largest_size);
    put_u64(w, r->ended_at);
}

static void put_event(Writer *w, const LifeEvent *e)
{
    put_u32(w, e->tick);
    put_bytes(w, &e->kind, 1);
    put_u32(w, e->other_id);
    put_i32(w, e->value);
}

static void put_tally(Writer *w, const LifeTally *t)
{
    put_u32(w, t->parents[0]);
    put_u32(w, t->parents[1]);
    put_u32(w, t->born);
    put_u32(w, t->kills);
    put_u32(w, t->hunter_kills);
    put_u32(w, t->children);
    put_u32(w, t->fights);
    put_u32(w, t->heavy_wounds);
    put_u32(w, t->kin_lost);
    put_u32(w, t->moves);
    put_float(w, t->max_size);
    put_bytes(w, &t->flags, 1);
    /* int8_t to unsigned char: -1 becomes 255, and get_tally turns it back. */
    unsigned char regions[2] = {(unsigned char)t->home_region,
                                (unsigned char)t->region_here};
    put_bytes(w, regions, sizeof(regions));
    put_u16(w, t->region_ticks);
}

/* A ring of events: its count, then every entry, used or not, so the newest
 * stays at the same place in the ring. */
static void put_events(Writer *w, uint32_t noted, const LifeEvent *e, int n)
{
    put_u32(w, noted);
    for (int i = 0; i < n; i++) {
        put_event(w, &e[i]);
    }
}

static void put_chronicle(Writer *w, const Chronicle *ch)
{
    for (int i = 0; i < CHRONICLE_NOTABLE; i++) {
        const NotableLife *n = &ch->lives[i];
        unsigned char flags[2] = {n->used ? 1 : 0, n->alive ? 1 : 0};
        put_bytes(w, flags, sizeof(flags));
        put_u32(w, n->id);
        put_u32(w, n->lineage);
        put_u32(w, n->age);
        put_float(w, n->score);
        put_tally(w, &n->tally);
        put_events(w, n->events_noted, n->events, NOTABLE_EVENTS);
    }
}

static void put_creature(Writer *w, const Creature *c)
{
    put_u32(w, c->id);
    put_u32(w, c->lineage);
    put_float(w, c->family_hue);
    put_i32(w, c->x);
    put_i32(w, c->y);
    put_bytes(w, &c->heading, 1);
    put_float(w, c->energy);
    put_float(w, c->health);
    put_float(w, c->grown);
    put_u32(w, c->hits_taken);
    for (int i = 0; i < HITS_KEPT; i++) {
        put_u32(w, c->recent_hits[i].by);
        put_u32(w, c->recent_hits[i].tick);
    }
    put_u32(w, c->age);
    put_bytes(w, &c->current.kind, 1);
    put_u16(w, c->current.hold);
    put_u16(w, c->hold_left);
    put_float(w, c->move_points);
    put_u16(w, c->mate_wish);
    put_float(w, c->level);
    put_float(w, c->eaten);
    put_plant(w, &c->gut_seed);
    put_u16(w, c->gut_ticks);
    put_floats(w, c->memory, MEMORY_VALUES);
    put_floats(w, c->genome.g, GENOME_LEN);
    put_tally(w, &c->tally);
    put_events(w, c->events_noted, c->events, CREATURE_EVENTS);
}

/*
 * Each slot starts with a byte: alive or free. A free slot keeps only its
 * link in the free list. The list's order must survive, because it decides
 * which slot the next birth takes, and slot order is the order accounting
 * walks the creatures in: a rebuilt list would be a different future.
 */
static void put_pool(Writer *w, const Pool *p)
{
    put_i32(w, p->free_head);
    put_u32(w, p->next_id);
    put_u32(w, p->births_refused);
    for (int i = 0; i < DEATH_COUNT; i++) {
        put_u32(w, p->deaths[i]);
    }
    for (int i = 0; i < p->capacity; i++) {
        const Creature *c = &p->items[i];
        unsigned char alive = c->alive ? 1 : 0;
        put_bytes(w, &alive, 1);
        if (c->alive) {
            put_creature(w, c);
        } else {
            put_i32(w, c->next_free);
        }
    }
}

static void put_everything(Writer *w, const Sim *s)
{
    put_bytes(w, SAVE_MAGIC, SAVE_MAGIC_LEN);
    put_u32(w, SAVE_VERSION);
    put_u32(w, (uint32_t)settings_count());
    put_u32(w, GENOME_LEN);
    put_u32(w, HISTORY);
    put_i32(w, s->world.w);
    put_i32(w, s->world.h);
    put_u64(w, s->seed);
    put_u64(w, s->tick);
    for (int i = 0; i < 4; i++) {
        put_u64(w, s->rng.s[i]);
    }
    put_settings(w, &s->set);
    put_world(w, &s->world);
    put_history(w, &s->stats.hist_plant_eaters);
    put_history(w, &s->stats.hist_meat_eaters);
    put_history(w, &s->stats.hist_planted);
    put_records(w, &s->records);
    put_pool(w, &s->pool);
    put_chronicle(w, &s->chronicle);
}

/*
 * Moves the finished file over the old save, as one step. Standard C leaves
 * open what rename does when the new name is taken: Linux replaces the old
 * file, and Windows refuses. So Windows gets its own call; only one of these
 * two bodies is compiled.
 */
static bool replace_file(const char *from, const char *to)
{
#ifdef _WIN32
    if (MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING)) {
        return true;
    }
    /* Windows calls report through GetLastError, and leave errno alone. The
     * caller prints errno, so it needs a meaningful value. */
    errno = EACCES;
    return false;
#else
    return rename(from, to) == 0;
#endif
}

bool save_write(const Sim *s, const char *path, char *err, size_t errlen)
{
    /* The file is built under another name and renamed when complete. On
     * one disk, rename replaces the old file in a single step: anyone
     * looking sees the whole old save or the whole new one, never half. */
    char tmp[4096];
    /* snprintf returns the length it wanted, so a result at or above the
     * size means the text was cut: this name must not be used cut. */
    int wanted = snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    if (wanted < 0 || (size_t)wanted >= sizeof(tmp)) {
        snprintf(err, errlen, "cannot save: the path is too long");
        return false;
    }
    /* "wb": the b means bytes as they are. It changes nothing on Linux,
     * and on Windows stops newlines being translated. */
    Writer w = {fopen(tmp, "wb"), true};
    if (w.f == NULL) {
        snprintf(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
        return false;
    }
    put_everything(&w, s);
    /* fclose can fail too, and often is where it fails: fwrite only filled
     * a buffer in memory, and fclose sends the last of it to the disk. */
    if (fclose(w.f) != 0) {
        w.ok = false;
    }
    if (!w.ok) {
        snprintf(err, errlen, "cannot write %s: %s", tmp, strerror(errno));
        remove(tmp);
        return false;
    }
    if (!replace_file(tmp, path)) {
        snprintf(err, errlen, "cannot write %s: %s", path, strerror(errno));
        remove(tmp);
        return false;
    }
    return true;
}

/* ---- reading ---- */

/*
 * The same sticky idea: the first problem is kept in problem, later reads
 * give zeros, and the checks along the way stop at the first problem. A
 * string literal lives for the whole run, so keeping a pointer to it is
 * safe.
 */
typedef struct {
    FILE *f;
    const char *problem; /* NULL while all is well */
    /* Room for the one message that has numbers in it. problem may point here:
     * this lives as long as the Reader does, which is long enough. */
    char version_problem[80];
} Reader;

static void fail(Reader *r, const char *problem)
{
    if (r->problem == NULL) {
        r->problem = problem;
    }
}

static void get_bytes(Reader *r, void *bytes, size_t n)
{
    if (r->problem != NULL || fread(bytes, 1, n, r->f) != n) {
        /* Zeros, not whatever the memory held: the caller may look at the
         * value before it checks for a problem. */
        memset(bytes, 0, n);
        fail(r, "the file is cut short");
    }
}

static uint32_t get_u32(Reader *r)
{
    unsigned char b[4];
    get_bytes(r, b, sizeof(b));
    /* b[3] is promoted to a signed int before the shift, and a byte from
     * 128 up shifted by 24 would not fit: undefined behaviour. Making it
     * uint32_t first keeps the shift unsigned. */
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

static uint64_t get_u64(Reader *r)
{
    uint64_t low = get_u32(r);
    uint64_t high = get_u32(r);
    return low | (high << 32);
}

/* Back from the bits to the int: memcpy again. A plain cast of a value
 * above INT_MAX gives an implementation-defined result. */
static int get_i32(Reader *r)
{
    uint32_t bits = get_u32(r);
    int32_t v;
    memcpy(&v, &bits, sizeof(v));
    return v;
}

static uint16_t get_u16(Reader *r)
{
    unsigned char b[2];
    get_bytes(r, b, sizeof(b));
    return (uint16_t)((unsigned)b[0] | ((unsigned)b[1] << 8));
}

static unsigned char get_byte(Reader *r)
{
    unsigned char b;
    get_bytes(r, &b, 1);
    return b;
}

/* A float, finite and within lo..hi, or a problem. Asks "inside?" rather
 * than "outside?": every comparison with NaN is false. */
static float get_float(Reader *r, float lo, float hi)
{
    uint32_t bits = get_u32(r);
    float v;
    memcpy(&v, &bits, sizeof(v));
    if (!(isfinite(v) && v >= lo && v <= hi)) {
        fail(r, "a number in the file is out of range");
        return 0.0f;
    }
    return v;
}

static bool get_settings(Reader *r, Settings *set, char *err, size_t errlen)
{
    for (int i = 0; i < settings_count(); i++) {
        const SettingInfo *info = settings_info(i);
        if (info->type == SETTING_INT) {
            *settings_int(set, info) = get_i32(r);
        } else if (info->type == SETTING_FLOAT) {
            uint32_t bits = get_u32(r);
            memcpy(settings_float(set, info), &bits, sizeof(bits));
        } else {
            get_bytes(r, settings_text(set, info), BRAIN_NAME_MAX);
        }
    }
    /* Before anything is allocated from them. */
    return r->problem == NULL && settings_check(set, err, errlen);
}

static void get_floats(Reader *r, float *v, size_t n, float lo, float hi)
{
    /* Stops at the first problem: a cut-short file need not be walked. */
    for (size_t i = 0; i < n && r->problem == NULL; i++) {
        v[i] = get_float(r, lo, hi);
    }
}

/* Reads n bytes and checks that none is above most. */
static void get_small(Reader *r, uint8_t *v, size_t n, unsigned most)
{
    get_bytes(r, v, n);
    for (size_t i = 0; i < n; i++) {
        if (v[i] > most) {
            fail(r, "a world layer holds an impossible value");
            return;
        }
    }
}

/* Genes stay within 0..1 by construction, and energy and fruit are only ever
 * compared and added, so a large one does no harm as long as it is finite. */
static void get_plant(Reader *r, Plant *p)
{
    p->energy = get_float(r, 0.0f, FLOAT_LIMIT);
    p->fruit = get_float(r, 0.0f, FLOAT_LIMIT);
    p->fruit_share = get_float(r, 0.0f, 1.0f);
    p->water_need = get_float(r, 0.0f, 1.0f);
    p->temp_pref = get_float(r, 0.0f, 1.0f);
    p->gut_time = get_float(r, 0.0f, 1.0f);
    /* The settings may differ from the ones it grew under, so only the widest
     * range any setting allows (tree_size_max, at most 100). */
    p->size = get_float(r, 0.0f, 100.0f);
}

static void get_world(Reader *r, World *w)
{
    size_t n = (size_t)w->w * (size_t)w->h;
    get_small(r, w->terrain, n, TERRAIN_SHALLOW);
    /* Fertility is 0..1 by construction (fertility_max is at most 1), and
     * drawing a cell multiplies it and converts the result to an int. */
    get_floats(r, w->fertility, n, 0.0f, 1.0f);
    get_small(r, w->veg, n, VEG_MAX);
    for (size_t i = 0; i < n && r->problem == NULL; i++) {
        w->veg_age[i] = get_u16(r);
    }
    get_small(r, w->veg_wither, n, UINT8_MAX);
    for (size_t i = 0; i < n && r->problem == NULL; i++) {
        get_plant(r, &w->plant[i]);
    }
    get_floats(r, w->carrion, n, 0.0f, FLOAT_LIMIT);
    get_floats(r, w->carrion_level, n, 0.0f, FLOAT_LIMIT);
    get_floats(r, w->scent, n, 0.0f, FLOAT_LIMIT);
    get_floats(r, w->water, n, 0.0f, 1.0f);
    get_floats(r, w->temperature, n, 0.0f, 1.0f);
    get_floats(r, w->rain, n, 0.0f, 1.0f);
    get_floats(r, w->snow, n, 0.0f, 1.0f);
    w->rain_seed = get_u64(r);
}

static void get_history(Reader *r, History *h)
{
    for (int i = 0; i < HISTORY; i++) {
        h->values[i] = get_i32(r);
        if (h->values[i] < 0 || h->values[i] > HISTORY_VALUE_MAX) {
            fail(r, "a history in the file is impossible");
        }
    }
    h->pos = get_i32(r);
    h->len = get_i32(r);
    /* pos is used as an array index by the next history_add. */
    if (h->pos < 0 || h->pos >= HISTORY || h->len < 0 || h->len > HISTORY) {
        fail(r, "a history in the file is impossible");
    }
}

/* Traits live in 0..1; weights within the setting's limit. */
static void get_genome(Reader *r, Genome *g, const Settings *set)
{
    get_floats(r, g->g, GENE_TRAITS, 0.0f, 1.0f);
    get_floats(r, g->g + GENE_TRAITS, NET_WEIGHTS, -set->weight_limit,
               set->weight_limit);
}

/* An event's kind picks its text and region a name, so both are checked; the
 * rest are numbers that are only ever printed. */
static void get_event(Reader *r, LifeEvent *e)
{
    e->tick = get_u32(r);
    e->kind = get_byte(r);
    e->other_id = get_u32(r);
    e->value = get_i32(r);
    if (e->kind >= LIFE_COUNT) {
        fail(r, "an event in the file is impossible");
    }
}

/* A region byte back to a REGION_ value: 255 was -1. */
static int8_t get_region(Reader *r)
{
    unsigned char b = get_byte(r);
    int region = b == 255 ? REGION_NONE : (int)b;
    if (region >= REGION_COUNT) {
        fail(r, "a region in the file is impossible");
        return REGION_NONE;
    }
    return (int8_t)region;
}

static void get_tally(Reader *r, LifeTally *t)
{
    t->parents[0] = get_u32(r);
    t->parents[1] = get_u32(r);
    t->born = get_u32(r);
    t->kills = get_u32(r);
    t->hunter_kills = get_u32(r);
    t->children = get_u32(r);
    t->fights = get_u32(r);
    t->heavy_wounds = get_u32(r);
    t->kin_lost = get_u32(r);
    t->moves = get_u32(r);
    t->max_size = get_float(r, 0.0f, FLOAT_LIMIT);
    t->flags = get_byte(r);
    t->home_region = get_region(r);
    t->region_here = get_region(r);
    t->region_ticks = get_u16(r);
}

static void get_events(Reader *r, uint32_t *noted, LifeEvent *e, int n)
{
    *noted = get_u32(r);
    for (int i = 0; i < n && r->problem == NULL; i++) {
        get_event(r, &e[i]);
    }
}

static void get_chronicle(Reader *r, Chronicle *ch)
{
    for (int i = 0; i < CHRONICLE_NOTABLE && r->problem == NULL; i++) {
        NotableLife *n = &ch->lives[i];
        unsigned char used = get_byte(r);
        unsigned char alive = get_byte(r);
        if (used > 1 || alive > 1) {
            fail(r, "a chronicle entry in the file is impossible");
            return;
        }
        n->used = used == 1;
        n->alive = alive == 1;
        n->id = get_u32(r);
        n->lineage = get_u32(r);
        n->age = get_u32(r);
        n->score = get_float(r, -FLOAT_LIMIT, FLOAT_LIMIT);
        get_tally(r, &n->tally);
        get_events(r, &n->events_noted, n->events, NOTABLE_EVENTS);
    }
}

/*
 * One living creature. Every check here guards something real: heading
 * indexes STEP_X, x and y become a cell index, and the current action's
 * kind picks what creature_act does. The genome goes last, so a bad
 * position is reported before thousands of floats are read.
 */
static void get_creature(Reader *r, Creature *c, const Settings *set,
                         const World *w)
{
    c->alive = true;
    c->next_free = -1;
    c->id = get_u32(r);
    c->lineage = get_u32(r);
    c->family_hue = get_float(r, 0.0f, 1.0f);
    c->x = get_i32(r);
    c->y = get_i32(r);
    c->heading = get_byte(r);
    c->energy = get_float(r, -FLOAT_LIMIT, FLOAT_LIMIT);
    c->health = get_float(r, -FLOAT_LIMIT, FLOAT_LIMIT);
    c->grown = get_float(r, 0.0f, FLOAT_LIMIT);
    c->hits_taken = get_u32(r);
    for (int i = 0; i < HITS_KEPT; i++) {
        c->recent_hits[i].by = get_u32(r);
        c->recent_hits[i].tick = get_u32(r);
    }
    c->age = get_u32(r);
    c->current.kind = get_byte(r);
    c->current.hold = get_u16(r);
    c->hold_left = get_u16(r);
    c->move_points = get_float(r, -FLOAT_LIMIT, FLOAT_LIMIT);
    c->mate_wish = get_u16(r);
    c->level = get_float(r, 0.0f, FLOAT_LIMIT);
    c->eaten = get_float(r, 0.0f, FLOAT_LIMIT);
    get_plant(r, &c->gut_seed);
    c->gut_ticks = get_u16(r);
    if (c->x < 0 || c->x >= w->w || c->y < 0 || c->y >= w->h ||
        c->heading >= HEADING_COUNT || c->current.kind >= ACT_COUNT) {
        fail(r, "a creature in the file is impossible");
        return;
    }
    get_floats(r, c->memory, MEMORY_VALUES, -1.0f, 1.0f);
    get_genome(r, &c->genome, set);
    get_tally(r, &c->tally);
    get_events(r, &c->events_noted, c->events, CREATURE_EVENTS);
}

/*
 * The free list must hold every dead slot exactly once, and nothing else.
 * A cycle would never end, and a living slot on the list would be
 * overwritten by the next birth. Counting steps catches both: more steps
 * than there are dead slots means a cycle.
 */
static void check_free_list(Reader *r, const Pool *p)
{
    int dead = p->capacity - p->count;
    int steps = 0;
    for (int i = p->free_head; i != -1; i = p->items[i].next_free) {
        if (i < 0 || i >= p->capacity || p->items[i].alive || steps >= dead) {
            fail(r, "the free list in the file is impossible");
            return;
        }
        steps++;
    }
    if (steps != dead) {
        fail(r, "the free list in the file is impossible");
    }
}

/* Reads every slot and rebuilds the occupant layer from where the
 * creatures say they stand. */
static void get_pool(Reader *r, Pool *p, World *w, const Settings *set)
{
    p->free_head = get_i32(r);
    p->next_id = get_u32(r);
    p->births_refused = get_u32(r);
    for (int i = 0; i < DEATH_COUNT; i++) {
        p->deaths[i] = get_u32(r);
    }
    p->count = 0;
    for (int i = 0; i < p->capacity && r->problem == NULL; i++) {
        Creature *c = &p->items[i];
        *c = (Creature){0};
        unsigned char alive = get_byte(r);
        if (alive == 0) {
            c->next_free = get_i32(r);
        } else if (alive == 1) {
            get_creature(r, c, set, w);
            if (r->problem != NULL) {
                return;
            }
            int cell = world_idx(w, c->x, c->y);
            if (!world_walkable(w, cell) || w->occupant[cell] >= 0) {
                fail(r, "a creature in the file stands where it cannot");
                return;
            }
            w->occupant[cell] = i;
            p->count++;
        } else {
            fail(r, "a creature slot in the file is impossible");
        }
    }
    if (r->problem == NULL) {
        check_free_list(r, p);
    }
}

/* The header up to the world's size; false with r->problem set. */
static bool get_header(Reader *r, int *width, int *height)
{
    char magic[SAVE_MAGIC_LEN];
    get_bytes(r, magic, sizeof(magic));
    if (r->problem != NULL) {
        return false;
    }
    if (memcmp(magic, SAVE_MAGIC, SAVE_MAGIC_LEN) != 0) {
        fail(r, "this is not an agon save file");
        return false;
    }
    uint32_t version = get_u32(r);
    if (r->problem == NULL && version != SAVE_VERSION) {
        /* problem points at text that must outlive this function, so the
         * wording goes into the Reader, not into a local array. */
        snprintf(r->version_problem, sizeof(r->version_problem),
                 "the file is save version %u; this agon reads only version %u",
                 (unsigned)version, SAVE_VERSION);
        fail(r, r->version_problem);
        return false;
    }
    uint32_t count = get_u32(r);
    uint32_t genome = get_u32(r);
    uint32_t history = get_u32(r);
    if (r->problem == NULL &&
        (count != (uint32_t)settings_count() || genome != GENOME_LEN ||
         history != HISTORY)) {
        fail(r, "the file comes from a different build of agon");
        return false;
    }
    *width = get_i32(r);
    *height = get_i32(r);
    /* Checked before allocating: the numbers decide how much memory to
     * ask for, and a damaged file must not get to ask for much. */
    if (r->problem == NULL && (*width < 1 || *width > OPTIONS_MAX_SIDE ||
                               *height < 1 || *height > OPTIONS_MAX_SIDE)) {
        fail(r, "the world's size in the file is impossible");
    }
    return r->problem == NULL;
}

/* The ids are only ever printed, so any value is harmless. peak is bounded so
 * that its text fits the screen; a date after the file's own tick cannot be. */
static void get_records(Reader *r, WorldRecords *rec, const Sim *s)
{
    rec->peak_population = get_i32(r);
    rec->oldest_id = get_u32(r);
    rec->oldest_age = get_u32(r);
    rec->largest_id = get_u32(r);
    rec->largest_size = get_float(r, 0.0f, FLOAT_LIMIT);
    rec->ended_at = get_u64(r);
    if (rec->peak_population < 0 ||
        rec->peak_population > s->pool.capacity || rec->ended_at > s->tick) {
        fail(r, "the records in the file are impossible");
    }
}

/* Reads the rest of the file into *s, already allocated. */
static void get_body(Reader *r, Sim *s, uint64_t seed, uint64_t tick,
                     const Rng *rng)
{
    s->seed = seed;
    s->tick = tick;
    s->rng = *rng;
    get_world(r, &s->world);
    get_history(r, &s->stats.hist_plant_eaters);
    get_history(r, &s->stats.hist_meat_eaters);
    get_history(r, &s->stats.hist_planted);
    get_records(r, &s->records, s);
    get_pool(r, &s->pool, &s->world, &s->set);
    get_chronicle(r, &s->chronicle);
    /* Only now is the population known. */
    if (s->records.ended_at != 0 && s->pool.count != 0) {
        fail(r, "the records in the file say a living world has ended");
    }
    /* The file must end exactly here. fgetc returns EOF (a negative int,
     * which is why it does not return a char) at the end. */
    if (r->problem == NULL && fgetc(r->f) != EOF) {
        fail(r, "the file has extra bytes at its end");
    }
}

bool save_read(Sim *s, const char *path, char *err, size_t errlen)
{
    *s = (Sim){0};
    Reader r = {.f = fopen(path, "rb")};
    if (r.f == NULL) {
        snprintf(err, errlen, "cannot open %s: %s", path, strerror(errno));
        return false;
    }
    int width = 0, height = 0;
    Settings set = settings_default(); /* every field is overwritten below */
    uint64_t seed = 0, tick = 0;
    Rng rng;
    char problem[256] = "";
    bool ok = false;
    if (get_header(&r, &width, &height)) {
        seed = get_u64(&r);
        tick = get_u64(&r);
        for (int i = 0; i < 4; i++) {
            rng.s[i] = get_u64(&r);
        }
        ok = get_settings(&r, &set, problem, sizeof(problem));
    }
    if (ok && !sim_alloc(s, &set, width, height)) {
        fclose(r.f);
        snprintf(err, errlen, "cannot load %s: out of memory", path);
        return false;
    }
    if (ok) {
        get_body(&r, s, seed, tick, &rng);
        ok = r.problem == NULL;
        if (ok && !sim_resume(s)) {
            snprintf(problem, sizeof(problem), "no brain called '%s'",
                     set.brain_name);
            ok = false;
        }
        if (!ok) {
            sim_free(s);
        }
    }
    fclose(r.f);
    if (!ok) {
        snprintf(err, errlen, "cannot load %s: %s", path,
                 r.problem != NULL ? r.problem : problem);
    }
    return ok;
}
