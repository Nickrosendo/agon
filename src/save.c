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
 *   i32 free_head, u32 next_id; then every creature slot.
 *
 * Change anything here and SAVE_VERSION goes up by one.
 */
#define SAVE_MAGIC "AGONSAVE"
#define SAVE_MAGIC_LEN 8
#define SAVE_VERSION 1u

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
    put_bytes(w, world->veg_rule, n);
    put_floats(w, world->carrion, n);
    put_floats(w, world->carrion_level, n);
    put_floats(w, world->scent, n);
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
static void put_creature(Writer *w, const Creature *c)
{
    put_u32(w, c->id);
    put_u32(w, c->lineage);
    put_i32(w, c->x);
    put_i32(w, c->y);
    put_bytes(w, &c->heading, 1);
    put_float(w, c->energy);
    put_u32(w, c->age);
    put_bytes(w, &c->current.kind, 1);
    put_u16(w, c->current.hold);
    put_u16(w, c->hold_left);
    put_float(w, c->move_points);
    put_float(w, c->level);
    put_float(w, c->eaten);
    put_floats(w, c->genome.g, GENOME_LEN);
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
    put_pool(w, &s->pool);
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

static void get_world(Reader *r, World *w)
{
    size_t n = (size_t)w->w * (size_t)w->h;
    get_small(r, w->terrain, n, TERRAIN_ROCK);
    /* Fertility is 0..1 by construction (fertility_max is at most 1), and
     * drawing a cell multiplies it and converts the result to an int. */
    get_floats(r, w->fertility, n, 0.0f, 1.0f);
    get_small(r, w->veg, n, VEG_MAX);
    for (size_t i = 0; i < n && r->problem == NULL; i++) {
        w->veg_age[i] = get_u16(r);
    }
    get_small(r, w->veg_wither, n, UINT8_MAX);
    get_small(r, w->veg_rule, n, VEG_RULE_USED_MASK);
    get_floats(r, w->carrion, n, 0.0f, FLOAT_LIMIT);
    get_floats(r, w->carrion_level, n, 0.0f, FLOAT_LIMIT);
    get_floats(r, w->scent, n, 0.0f, FLOAT_LIMIT);
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
    c->x = get_i32(r);
    c->y = get_i32(r);
    c->heading = get_byte(r);
    c->energy = get_float(r, -FLOAT_LIMIT, FLOAT_LIMIT);
    c->age = get_u32(r);
    c->current.kind = get_byte(r);
    c->current.hold = get_u16(r);
    c->hold_left = get_u16(r);
    c->move_points = get_float(r, -FLOAT_LIMIT, FLOAT_LIMIT);
    c->level = get_float(r, 0.0f, FLOAT_LIMIT);
    c->eaten = get_float(r, 0.0f, FLOAT_LIMIT);
    if (c->x < 0 || c->x >= w->w || c->y < 0 || c->y >= w->h ||
        c->heading >= HEADING_COUNT || c->current.kind >= ACT_COUNT) {
        fail(r, "a creature in the file is impossible");
        return;
    }
    get_genome(r, &c->genome, set);
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
            if (w->terrain[cell] != TERRAIN_SOIL || w->occupant[cell] >= 0) {
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
        fail(r, "the file has a different version");
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
    get_pool(r, &s->pool, &s->world, &s->set);
    /* The file must end exactly here. fgetc returns EOF (a negative int,
     * which is why it does not return a char) at the end. */
    if (r->problem == NULL && fgetc(r->f) != EOF) {
        fail(r, "the file has extra bytes at its end");
    }
}

bool save_read(Sim *s, const char *path, char *err, size_t errlen)
{
    *s = (Sim){0};
    Reader r = {fopen(path, "rb"), NULL};
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
