#include "test.h"

#include <stdlib.h> /* malloc, free */
#include <string.h> /* strstr, memcmp */
#ifdef _WIN32
#include <direct.h> /* _mkdir, _rmdir: Windows's own names */
/* Windows's mkdir has no permissions argument, so one name cannot do for both. */
#define make_folder(path) _mkdir(path)
#define remove_folder(path) _rmdir(path)
#else
#include <sys/stat.h> /* mkdir (POSIX) */
#include <unistd.h>   /* rmdir (POSIX) */
#define make_folder(path) mkdir(path, 0700)
#define remove_folder(path) rmdir(path)
#endif

#include "chronicle.h"
#include "climate.h"
#include "genome.h"
#include "rng.h"
#include "save.h"
#include "settings.h"
#include "sim.h"

/* Under build/, which git ignores. Tests run from the top folder. */
#define SAVE_FILE "build/test_save.save"
#define BROKEN_FILE "build/test_save_broken.save"

/* The whole of a file, on the heap; the caller frees it. */
static unsigned char *read_whole(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long end = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *bytes = malloc((size_t)end);
    if (bytes != NULL && fread(bytes, 1, (size_t)end, f) != (size_t)end) {
        free(bytes);
        bytes = NULL;
    }
    fclose(f);
    *size = (size_t)end;
    return bytes;
}

static void write_whole(const char *path, const unsigned char *bytes,
                        size_t size)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (f != NULL) {
        CHECK(fwrite(bytes, 1, size, f) == size);
        fclose(f);
    }
}

/* Places exactly count creatures on the first soil cells with random genomes,
 * for tests that depend on the count: founders are a share of the soil, and a
 * tiny world's share is hard to hit exactly. */
static void place_founders(Sim *s, int count)
{
    for (int i = 0; i < s->world.w * s->world.h && s->pool.count < count; i++) {
        int slot = pool_spawn(&s->pool, &s->world, i % s->world.w,
                              i / s->world.w);
        if (slot >= 0) {
            s->pool.items[slot].energy = s->set.energy_start;
            genome_random(&s->pool.items[slot].genome, &s->rng, &s->set);
        }
    }
}

static bool history_same(const History *a, const History *b)
{
    if (a->len != b->len) {
        return false;
    }
    for (int k = 0; k < a->len; k++) {
        if (history_at(a, k) != history_at(b, k)) {
            return false;
        }
    }
    return true;
}

/*
 * The main promise: a loaded world is the same world, and it goes on to
 * do exactly what the original does. The second half matters most: state
 * that is missing from the checksum but changes the future (the order of
 * the free list, the random generator) shows up only by running on.
 */
static void test_round_trip_and_continue(void)
{
    Settings set = settings_default();
    Sim a, b;
    char err[256] = "";
    CHECK(sim_init(&a, &set, 7, 64, 32));
    for (int i = 0; i < 1000; i++) {
        sim_step(&a);
    }
    /* A seed being carried travels too, or the world would differ when it drops. */
    for (int i = 0; i < a.pool.capacity; i++) {
        if (a.pool.items[i].alive) {
            a.pool.items[i].gut_seed = (Plant){.fruit_share = 0.75f, .temp_pref = 0.25f};
            a.pool.items[i].gut_ticks = 9;
            break;
        }
    }
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(b.seed == 7 && b.tick == 1000);
    CHECK(b.world.w == 64 && b.world.h == 32);
    CHECK(b.pool.free_head == a.pool.free_head);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    CHECK(history_same(&a.stats.hist_plant_eaters, &b.stats.hist_plant_eaters));
    CHECK(history_same(&a.stats.hist_meat_eaters, &b.stats.hist_meat_eaters));
    CHECK(history_same(&a.stats.hist_planted, &b.stats.hist_planted));
    CHECK(b.stats.creatures == a.pool.count);
    Settings copied = a.set;
    CHECK(memcmp(&copied, &b.set, sizeof(copied)) == 0);

    for (int i = 0; i < 1000; i++) {
        sim_step(&a);
        sim_step(&b);
    }
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    sim_free(&a);
    sim_free(&b);
}

/* Life stories travel with the file: every creature's ring and tally and the
 * chronicle's table come back the same, and keep growing the same way. */
static void test_chronicle_saved(void)
{
    Settings set = settings_default();
    Sim a, b;
    char err[256] = "";
    CHECK(sim_init(&a, &set, 5, 64, 32));
    for (int i = 0; i < 1500; i++) {
        sim_step(&a);
    }
    CHECK(a.chronicle.lives[0].used); /* there is a story to keep */
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(sim_story_checksum(&a) == sim_story_checksum(&b));
    for (int i = 0; i < 500; i++) {
        sim_step(&a);
        sim_step(&b);
    }
    CHECK(sim_story_checksum(&a) == sim_story_checksum(&b));
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    sim_free(&a);
    sim_free(&b);
}

static void test_settings_travel_with_the_file(void)
{
    Settings set = settings_default();
    set.max_age = 321;
    set.mutation_rate = 0.125f;
    Sim a, b;
    char err[256] = "";
    CHECK(sim_init(&a, &set, 3, 16, 16));
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(b.set.max_age == 321);
    CHECK(b.set.mutation_rate == 0.125f);
    CHECK(strcmp(b.set.brain_name, "net") == 0);
    sim_free(&a);
    sim_free(&b);
}

/* A brain with private state starts it afresh: no crash, and it runs. */
static void test_random_brain_reopens(void)
{
    Settings set = settings_default();
    snprintf(set.brain_name, sizeof(set.brain_name), "random");
    Sim a, b;
    char err[256] = "";
    CHECK(sim_init(&a, &set, 4, 16, 16));
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(strcmp(b.brain.name, "random") == 0);
    for (int i = 0; i < 20; i++) {
        sim_step(&b);
    }
    sim_free(&a);
    sim_free(&b);
}

/* A small world and a file of it, for the tests of damaged files. */
static unsigned char *small_save(size_t *size)
{
    Settings set = settings_default();
    set.pool_capacity = 40;
    set.start_share = 0.0f;
    set.min_creatures = 20;
    Sim a;
    char err[256] = "";
    CHECK(sim_init(&a, &set, 11, 16, 16));
    place_founders(&a, 20);
    for (int i = 0; i < 150; i++) {
        sim_step(&a);
    }
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    sim_free(&a);
    return read_whole(SAVE_FILE, size);
}

/* Must fail, with a message mentioning piece. AddressSanitizer's leak check
 * at exit proves a failed load leaves nothing to free. */
static bool load_fails(const unsigned char *bytes, size_t size,
                       const char *piece)
{
    write_whole(BROKEN_FILE, bytes, size);
    Sim s;
    char err[256] = "";
    if (save_read(&s, BROKEN_FILE, err, sizeof(err))) {
        sim_free(&s);
        return false;
    }
    return strstr(err, piece) != NULL;
}

/* A wish to mate and the births a full pool refused are state: without them a
 * loaded world would go on differently. */
static void test_mate_wish_survives_save(void)
{
    Settings set = settings_default();
    set.start_share = 0.01f;
    Sim a;
    CHECK(sim_init(&a, &set, 21, 32, 32));
    int slot = -1;
    for (int i = 0; i < a.pool.capacity && slot < 0; i++) {
        if (a.pool.items[i].alive) {
            slot = i;
        }
    }
    CHECK(slot >= 0);
    a.pool.items[slot].mate_wish = 3;
    a.pool.births_refused = 7;
    a.pool.deaths[DEATH_HUNGER] = 11;
    a.pool.items[slot].health = 12.5f;
    a.pool.items[slot].grown = 3.25f;
    a.pool.items[slot].family_hue = 0.125f;
    a.pool.items[slot].memory[1] = -0.75f;
    a.pool.items[slot].hits_taken = 2;
    a.pool.items[slot].recent_hits[1] = (Hit){.by = 42, .tick = 77};
    char err[256] = "";
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    Sim b;
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(b.pool.items[slot].mate_wish == 3);
    CHECK(b.pool.births_refused == 7);
    CHECK(b.pool.deaths[DEATH_HUNGER] == 11);
    CHECK(b.pool.items[slot].health == 12.5f);
    CHECK(b.pool.items[slot].grown == 3.25f);
    CHECK(b.pool.items[slot].family_hue == 0.125f);
    CHECK(b.pool.items[slot].memory[1] == -0.75f);
    CHECK(b.pool.items[slot].hits_taken == 2);
    CHECK(b.pool.items[slot].recent_hits[1].by == 42);
    CHECK(b.pool.items[slot].recent_hits[1].tick == 77);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    sim_free(&a);
    sim_free(&b);
    remove(SAVE_FILE);
}

static void test_wrong_magic_and_version(void)
{
    size_t size = 0;
    unsigned char *bytes = small_save(&size);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    bytes[0] ^= 0x01;
    CHECK(load_fails(bytes, size, "not an agon save"));
    bytes[0] ^= 0x01;
    bytes[8] ^= 0x40; /* the version comes right after the 8-byte magic */
    CHECK(load_fails(bytes, size, "version"));
    bytes[8] ^= 0x40;
    /* An old save says which version it is, little end first. */
    unsigned char kept = bytes[8];
    bytes[8] = 1;
    CHECK(load_fails(bytes, size, "version 1"));
    bytes[8] = kept;
    /* And the same bytes, untouched, are fine: the failures above came
     * from the changes. */
    write_whole(BROKEN_FILE, bytes, size);
    Sim s;
    char err[256] = "";
    CHECK(save_read(&s, BROKEN_FILE, err, sizeof(err)));
    sim_free(&s);
    free(bytes);
}

static void test_cut_short(void)
{
    size_t size = 0;
    unsigned char *bytes = small_save(&size);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    /* Ten points through the file, then the very first and last bytes. */
    for (size_t point = 0; point < 10; point++) {
        CHECK(load_fails(bytes, size * point / 10, "cut short"));
    }
    CHECK(load_fails(bytes, 3, "cut short"));
    CHECK(load_fails(bytes, size - 1, "cut short"));
    free(bytes);
}

static void test_extra_bytes(void)
{
    size_t size = 0;
    unsigned char *bytes = small_save(&size);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    unsigned char *longer = malloc(size + 1);
    CHECK(longer != NULL);
    if (longer != NULL) {
        memcpy(longer, bytes, size);
        longer[size] = 0;
        CHECK(load_fails(longer, size + 1, "extra"));
        free(longer);
    }
    free(bytes);
}

static void test_missing_file_and_bad_path(void)
{
    Sim s;
    char err[256] = "";
    CHECK(!save_read(&s, "build/no_such.save", err, sizeof(err)));
    CHECK(strstr(err, "build/no_such.save") != NULL);

    Settings set = settings_default();
    Sim a;
    CHECK(sim_init(&a, &set, 3, 8, 8));
    CHECK(!save_write(&a, "build/no_such_folder/x.save", err, sizeof(err)));
    CHECK(strstr(err, "no_such_folder") != NULL);
    sim_free(&a);
}

/* A failed write must not hurt the save that is already there. A folder
 * sits where the temporary file wants to go, so the write cannot start. */
static void test_failed_write_keeps_old_file(void)
{
    size_t size = 0;
    unsigned char *before = small_save(&size);
    CHECK(before != NULL);
    if (before == NULL) {
        return;
    }
    char tmp_path[128];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", SAVE_FILE);
    CHECK(make_folder(tmp_path) == 0);

    Settings set = settings_default();
    Sim a;
    char err[256] = "";
    CHECK(sim_init(&a, &set, 5, 8, 8));
    CHECK(!save_write(&a, SAVE_FILE, err, sizeof(err)));
    sim_free(&a);
    CHECK(remove_folder(tmp_path) == 0);

    size_t after_size = 0;
    unsigned char *after = read_whole(SAVE_FILE, &after_size);
    CHECK(after != NULL && after_size == size);
    CHECK(after != NULL && memcmp(after, before, size) == 0);
    free(before);
    free(after);
}

/*
 * Saving twice to one path: the second replaces the first. Linux's rename does
 * that by itself; Windows's refuses when the name is taken, so this test is
 * here for Windows.
 */
static void test_save_over_existing_file(void)
{
    Settings set = settings_default();
    Sim first, second, loaded;
    char err[256] = "";
    CHECK(sim_init(&first, &set, 5, 8, 8));
    CHECK(sim_init(&second, &set, 6, 8, 8));
    CHECK(save_write(&first, SAVE_FILE, err, sizeof(err)));
    CHECK(save_write(&second, SAVE_FILE, err, sizeof(err)));
    CHECK(save_read(&loaded, SAVE_FILE, err, sizeof(err)));
    CHECK(sim_checksum(&loaded) == sim_checksum(&second));
    CHECK(sim_checksum(&loaded) != sim_checksum(&first));
    sim_free(&loaded);
    sim_free(&second);
    sim_free(&first);
}

/*
 * The layout of the smallest useful file, spelled out here on purpose: a
 * test written from the format, not from the code that writes it. The
 * world has one living creature in slot 0 and one free slot, 1. Positions
 * count from the file's start or, for the pool, from its end.
 */
#define OFF_VERSION 8
#define OFF_WIDTH 24
#define OFF_HEIGHT 28
#define OFF_TICK 40
#define OFF_SETTINGS 80
#define OFF_SIZE_FREE_SLOT 5 /* alive byte, next_free */

/* The bytes from the living creature's alive byte, in the file's order. */
enum {
    AT_ALIVE = 0,
    AT_ID = 1,
    AT_LINEAGE = 5,
    AT_FAMILY_HUE = 9,
    AT_X = 13,
    AT_Y = 17,
    AT_HEADING = 21,
    AT_ENERGY = 22,
    AT_HEALTH = 26,
    AT_GROWN = 30,
    AT_HITS_TAKEN = 34,
    AT_RECENT_HITS = 38, /* HITS_KEPT pairs of u32: 64 bytes */
    AT_AGE = 102,
    AT_KIND = 106,
    AT_HOLD = 107,
    AT_HOLD_LEFT = 109,
    AT_MOVE_POINTS = 111,
    AT_MATE_WISH = 115,
    AT_LEVEL = 117,
    AT_EATEN = 121,
    AT_GUT_SEED = 125, /* six floats: 24 bytes */
    AT_GUT_TICKS = 149,
    AT_MEMORY = 151,
    AT_GENOME = 163,
    /* The life story: ten u32 counts, max_size, flags, two region bytes and
     * region_ticks (49 bytes); then events_noted and 8 events of 13 bytes each
     * (tick, kind, other_id, value: the struct's 3 padding bytes are not written). */
    AT_TALLY = AT_GENOME + 4 * GENOME_LEN,
    TALLY_BYTES = 10 * 4 + 4 + 1 + 2 + 2,
    AT_TALLY_HOME = AT_TALLY + 10 * 4 + 4 + 1,
    AT_EVENTS_NOTED = AT_TALLY + TALLY_BYTES,
    EVENT_BYTES = 4 + 1 + 4 + 4,
    AT_EVENTS = AT_EVENTS_NOTED + 4,
    SLOT_LIVING_BYTES = AT_EVENTS + CREATURE_EVENTS * EVENT_BYTES,
    /* The chronicle's table after every slot: per entry, used and alive bytes,
     * id, lineage, age, score, a tally, and a ring of NOTABLE_EVENTS. */
    NOTABLE_BYTES = 2 + 4 * 4 + TALLY_BYTES + 4 + NOTABLE_EVENTS * EVENT_BYTES,
    CHRONICLE_BYTES = CHRONICLE_NOTABLE * NOTABLE_BYTES,
};

typedef struct {
    unsigned char *bytes;
    size_t size;
    size_t layers;    /* where the world's layers start */
    size_t histories; /* where the three histories start */
    size_t records;   /* where the world's records start */
    size_t pool;      /* where free_head is */
    size_t living;    /* slot 0's alive byte */
    size_t free_slot; /* slot 1's alive byte */
} Tiny;

static void put_le32(unsigned char *at, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        at[i] = (unsigned char)((v >> (8 * i)) & 0xFFu);
    }
}

static uint32_t get_le32(const unsigned char *at)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)at[i] << (8 * i);
    }
    return v;
}

/* NaN and a huge number, as bit patterns of a float. */
#define BITS_NAN 0x7FC00000u
#define BITS_HUGE 0x7F000000u /* about 1.7e38 */

static Tiny tiny_save(void)
{
    Settings set = settings_default();
    set.pool_capacity = 2;
    set.start_share = 0.0f;
    set.min_creatures = 1;
    Sim a;
    char err[256] = "";
    /* An 8 by 8 world could be all rock and water for some seeds; the
     * count check below fails if this seed ever has no soil. */
    CHECK(sim_init(&a, &set, 3, 8, 8));
    place_founders(&a, 1);
    CHECK(a.pool.count == 1 && a.pool.items[0].alive);
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    sim_free(&a);

    Tiny t = {0};
    t.bytes = read_whole(SAVE_FILE, &t.size);
    size_t settings_bytes = 0;
    for (int i = 0; i < settings_count(); i++) {
        settings_bytes +=
            settings_info(i)->type == SETTING_TEXT ? BRAIN_NAME_MAX : 4;
    }
    t.layers = OFF_SETTINGS + settings_bytes;
    /* terrain, fertility, veg, veg_age, veg_wither, plant, carrion,
     * carrion_level, scent, water, temperature, rain, for 64 cells, and the
     * rain's seed once. */
    t.histories = t.layers + 64 * (1 + 4 + 1 + 2 + 1 + 24 + 4 + 4 + 4 + 4 + 4 + 4) + 8;
    t.records = t.histories + 3 * (HISTORY * 4 + 8);
    /* peak, oldest id and age, largest id and size, then the u64 ended_at. */
    t.pool = t.records + 5 * 4 + 8;
    /* free_head, next_id, births_refused, deaths by cause */
    t.living = t.pool + 12 + 4 * DEATH_COUNT;
    t.free_slot = t.living + SLOT_LIVING_BYTES;
    CHECK(t.size == t.free_slot + OFF_SIZE_FREE_SLOT + CHRONICLE_BYTES);
    return t;
}

/* The file with one 32-bit value changed must be refused, naming piece. */
static void refuses_u32(const Tiny *t, size_t at, uint32_t value,
                        const char *piece)
{
    unsigned char *copy = malloc(t->size);
    CHECK(copy != NULL);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, t->bytes, t->size);
    put_le32(copy + at, value);
    if (!load_fails(copy, t->size, piece)) {
        fprintf(stderr, "offset %zu, value 0x%x: expected a refusal with "
                        "'%s'\n", at, value, piece);
        CHECK(false);
    }
    free(copy);
}

static void refuses_byte(const Tiny *t, size_t at, unsigned char value,
                         const char *piece)
{
    unsigned char *copy = malloc(t->size);
    CHECK(copy != NULL);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, t->bytes, t->size);
    copy[at] = value;
    if (!load_fails(copy, t->size, piece)) {
        fprintf(stderr, "offset %zu, byte 0x%x: expected a refusal with "
                        "'%s'\n", at, value, piece);
        CHECK(false);
    }
    free(copy);
}

static void test_layout_is_as_documented(void)
{
    Tiny t = tiny_save();
    CHECK(t.bytes != NULL);
    if (t.bytes == NULL) {
        return;
    }
    CHECK(memcmp(t.bytes, "AGONSAVE", 8) == 0);
    CHECK(get_le32(t.bytes + OFF_WIDTH) == 8);
    CHECK(get_le32(t.bytes + OFF_HEIGHT) == 8);
    CHECK(t.bytes[t.living + AT_ALIVE] == 1);
    CHECK(t.bytes[t.free_slot + AT_ALIVE] == 0);
    CHECK(t.bytes[t.living + AT_KIND] < ACT_COUNT);
    free(t.bytes);
}

static void test_refuses_impossible_header(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    refuses_u32(&t, OFF_VERSION + 4, 1, "different build"); /* settings */
    refuses_u32(&t, OFF_VERSION + 8, 1, "different build"); /* genome */
    refuses_u32(&t, OFF_VERSION + 12, 1, "different build"); /* history */
    refuses_u32(&t, OFF_WIDTH, 0, "size");
    refuses_u32(&t, OFF_WIDTH, 0xFFFFFFFFu, "size");
    refuses_u32(&t, OFF_HEIGHT, 100000, "size");
    free(t.bytes);
}

static void test_refuses_impossible_settings(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    size_t at = OFF_SETTINGS;
    for (int i = 0; i < settings_count(); i++) {
        const SettingInfo *info = settings_info(i);
        if (info->type == SETTING_TEXT) {
            /* No '\0' anywhere in the name: not a string. */
            unsigned char *copy = malloc(t.size);
            CHECK(copy != NULL);
            if (copy != NULL) {
                memcpy(copy, t.bytes, t.size);
                memset(copy + at, 'x', BRAIN_NAME_MAX);
                CHECK(load_fails(copy, t.size, info->name));
                free(copy);
            }
            /* A proper name no brain has. */
            copy = malloc(t.size);
            CHECK(copy != NULL);
            if (copy != NULL) {
                memcpy(copy, t.bytes, t.size);
                memcpy(copy + at, "nope", 5);
                CHECK(load_fails(copy, t.size, "nope"));
                free(copy);
            }
            at += BRAIN_NAME_MAX;
        } else if (info->type == SETTING_INT) {
            refuses_u32(&t, at, 0xFFFFFFFFu, info->name); /* -1 */
            at += 4;
        } else {
            refuses_u32(&t, at, BITS_NAN, info->name);
            at += 4;
        }
    }
    free(t.bytes);
}

/* The first cell of each layer, with an impossible value. */
static void test_refuses_impossible_layers(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    size_t terrain = t.layers;
    size_t fertility = terrain + 64;
    size_t veg = fertility + 64 * 4;
    size_t veg_age = veg + 64;
    size_t veg_wither = veg_age + 64 * 2;
    size_t plant = veg_wither + 64;
    size_t carrion = plant + 64 * 24;
    size_t carrion_level = carrion + 64 * 4;
    size_t scent = carrion_level + 64 * 4;
    refuses_byte(&t, terrain, TERRAIN_SHALLOW + 1, "layer");
    refuses_u32(&t, fertility, BITS_NAN, "out of range");
    refuses_u32(&t, fertility, 0x40000000u /* 2.0 */, "out of range");
    refuses_u32(&t, fertility, BITS_HUGE, "out of range");
    refuses_byte(&t, veg, VEG_MAX + 1, "layer");
    /* A plant's energy first, then its fruit, then the genes, which are 0..1. */
    refuses_u32(&t, plant, BITS_NAN, "out of range");
    refuses_u32(&t, plant + 4, BITS_HUGE, "out of range");
    refuses_u32(&t, plant + 8, 0x40000000u /* 2.0 */, "out of range");
    refuses_u32(&t, plant + 20, BITS_NAN, "out of range");
    refuses_u32(&t, carrion, BITS_NAN, "out of range");
    refuses_u32(&t, carrion, BITS_HUGE, "out of range");
    refuses_u32(&t, carrion_level, BITS_NAN, "out of range");
    refuses_u32(&t, scent, BITS_NAN, "out of range");
    /* Drawing turns water into an array index, so it must stay within 0..1. */
    size_t water = scent + 64 * 4;
    size_t temperature = water + 64 * 4;
    size_t rain = temperature + 64 * 4;
    refuses_u32(&t, water, BITS_NAN, "out of range");
    refuses_u32(&t, water, 0x40000000u /* 2.0 */, "out of range");
    refuses_u32(&t, temperature, BITS_NAN, "out of range");
    refuses_u32(&t, rain, BITS_NAN, "out of range");
    free(t.bytes);
}

static void test_refuses_impossible_histories(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    size_t pos = t.histories + HISTORY * 4;
    refuses_u32(&t, t.histories, 0xFFFFFFFFu, "history"); /* value -1 */
    refuses_u32(&t, t.histories, 0x7FFFFFFFu, "history"); /* value huge */
    refuses_u32(&t, pos, HISTORY, "history");
    refuses_u32(&t, pos, 0xFFFFFFFFu, "history");
    refuses_u32(&t, pos + 4, HISTORY + 1, "history");
    refuses_u32(&t, pos + 4, 0xFFFFFFFFu, "history");
    free(t.bytes);
}

static void test_refuses_impossible_records(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    refuses_u32(&t, t.records, 0xFFFFFFFFu, "records");    /* peak of -1 */
    refuses_u32(&t, t.records, 3, "records");              /* a pool of 2 */
    refuses_u32(&t, t.records + 16, BITS_NAN, "number");   /* largest size */
    refuses_u32(&t, t.records + 16, 0xBF800000u, "number"); /* -1.0 */
    /* Ended after the file's own tick, which is 0. */
    refuses_u32(&t, t.records + 20, 1, "records");
    refuses_u32(&t, t.records + 24, 1, "records");
    /* A plausible date, tick 1 of 1, but a creature is alive. */
    unsigned char *copy = malloc(t.size);
    CHECK(copy != NULL);
    if (copy != NULL) {
        memcpy(copy, t.bytes, t.size);
        put_le32(copy + OFF_TICK, 1);
        put_le32(copy + t.records + 20, 1);
        CHECK(load_fails(copy, t.size, "living world"));
        free(copy);
    }
    free(t.bytes);
}

/* A world after its last death saves and loads like any other, and stays over. */
static void test_dead_world_round_trip(void)
{
    Settings set = settings_default();
    Sim a;
    CHECK(sim_init(&a, &set, 5, 64, 32));
    for (int i = 0; i < a.pool.capacity; i++) {
        if (a.pool.items[i].alive) {
            pool_kill(&a.pool, &a.world, i, &a.set, DEATH_WOUNDS);
        }
    }
    sim_step(&a);
    CHECK(sim_is_over(&a));
    CHECK(a.records.ended_at == a.tick);
    char err[256] = "";
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    Sim b;
    CHECK(save_read(&b, SAVE_FILE, err, sizeof(err)));
    CHECK(sim_is_over(&b));
    CHECK(b.records.peak_population == a.records.peak_population);
    CHECK(b.records.oldest_id == a.records.oldest_id);
    CHECK(b.records.oldest_age == a.records.oldest_age);
    CHECK(b.records.largest_id == a.records.largest_id);
    CHECK(b.records.largest_size == a.records.largest_size);
    CHECK(b.records.ended_at == a.records.ended_at);
    CHECK(sim_checksum(&a) == sim_checksum(&b));
    CHECK(b.stats.creatures == 0);
    sim_free(&a);
    sim_free(&b);
}

static void test_refuses_impossible_creature(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    size_t c = t.living;
    refuses_byte(&t, c + AT_ALIVE, 2, "slot");
    refuses_u32(&t, c + AT_X, 8, "creature");
    refuses_u32(&t, c + AT_X, 0xFFFFFFFFu, "creature");
    refuses_u32(&t, c + AT_Y, 8, "creature");
    refuses_u32(&t, c + AT_Y, 0xFFFFFFFFu, "creature");
    refuses_byte(&t, c + AT_HEADING, HEADING_COUNT, "creature");
    refuses_byte(&t, c + AT_KIND, ACT_COUNT, "creature");
    refuses_u32(&t, c + AT_ENERGY, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_ENERGY, BITS_HUGE, "out of range");
    refuses_u32(&t, c + AT_HEALTH, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_GROWN, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_MEMORY, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_FAMILY_HUE, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_MEMORY, 0x40000000u /* 2.0 */, "out of range");
    refuses_u32(&t, c + AT_GROWN, 0xBF800000u /* -1.0 */, "out of range");
    refuses_u32(&t, c + AT_MOVE_POINTS, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_LEVEL, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_EATEN, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_GENOME, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_GENOME, 0x40000000u /* 2.0, a trait */,
                "out of range");
    refuses_u32(&t, c + AT_GENOME + 4 * GENE_TRAITS, BITS_NAN, "out of range");
    refuses_u32(&t, c + AT_GENOME + 4 * GENE_TRAITS, BITS_HUGE, "out of range");
    /* Its story: an event's kind picks its text, and a region its name. */
    refuses_byte(&t, c + AT_EVENTS + 4, LIFE_COUNT, "event");
    refuses_byte(&t, c + AT_TALLY_HOME, REGION_COUNT, "region");
    refuses_u32(&t, c + AT_TALLY + 10 * 4, BITS_NAN, "out of range");
    /* The table's first entry: its used byte, and its first event's kind. */
    size_t entry = t.free_slot + OFF_SIZE_FREE_SLOT;
    refuses_byte(&t, entry, 2, "chronicle");
    refuses_byte(&t, entry + 2 + 4 * 4 + TALLY_BYTES + 4 + 4, LIFE_COUNT,
                 "event");
    free(t.bytes);
}

/* A creature standing on a cell that is not soil. */
static void test_refuses_creature_off_soil(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    /* Change the terrain under the creature instead: same effect, and no
     * dependence on what this seed's world looks like. */
    uint32_t x = get_le32(t.bytes + t.living + AT_X);
    uint32_t y = get_le32(t.bytes + t.living + AT_Y);
    refuses_byte(&t, t.layers + y * 8 + x, TERRAIN_WATER, "cannot");
    free(t.bytes);
}

static void test_refuses_broken_free_list(void)
{
    Tiny t = tiny_save();
    if (t.bytes == NULL) {
        CHECK(false);
        return;
    }
    /* free_head: should be 1, the free slot. */
    refuses_u32(&t, t.pool, 0, "free list");          /* a living slot */
    refuses_u32(&t, t.pool, 2, "free list");          /* past the end */
    refuses_u32(&t, t.pool, 0xFFFFFFFEu, "free list"); /* -2 */
    refuses_u32(&t, t.pool, 0xFFFFFFFFu, "free list"); /* -1: a slot lost */
    /* The free slot's next_free: should be -1. */
    size_t next = t.free_slot + 1;
    refuses_u32(&t, next, 1, "free list"); /* a cycle: itself */
    refuses_u32(&t, next, 0, "free list"); /* a living slot */
    refuses_u32(&t, next, 2, "free list"); /* past the end */
    free(t.bytes);
}

/* Two creatures on one cell: the second slot's place taken from the first. */
static void test_refuses_two_on_one_cell(void)
{
    Settings set = settings_default();
    set.pool_capacity = 2;
    set.start_share = 0.0f;
    set.min_creatures = 2;
    Sim a;
    char err[256] = "";
    CHECK(sim_init(&a, &set, 3, 8, 8));
    place_founders(&a, 2);
    CHECK(a.pool.count == 2);
    CHECK(save_write(&a, SAVE_FILE, err, sizeof(err)));
    sim_free(&a);
    size_t size = 0;
    unsigned char *bytes = read_whole(SAVE_FILE, &size);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    size_t second = size - CHRONICLE_BYTES - SLOT_LIVING_BYTES;
    size_t first = second - SLOT_LIVING_BYTES;
    memcpy(bytes + second + AT_X, bytes + first + AT_X, 8); /* x and y */
    CHECK(load_fails(bytes, size, "cannot"));
    free(bytes);
}

/*
 * The file is not trusted. One wrong byte, wherever it is, must end in
 * either a clean refusal or a world that runs without touching memory it
 * should not (AddressSanitizer and the undefined-behaviour checker watch
 * this test). This is a fuzz test: many random small damages.
 */
static void try_damage(const unsigned char *bytes, size_t size, size_t at,
                       unsigned char mask)
{
    unsigned char *copy = malloc(size);
    CHECK(copy != NULL);
    if (copy == NULL) {
        return;
    }
    memcpy(copy, bytes, size);
    copy[at] ^= mask;
    write_whole(BROKEN_FILE, copy, size);
    free(copy);
    Sim s;
    char err[256] = "";
    if (save_read(&s, BROKEN_FILE, err, sizeof(err))) {
        for (int i = 0; i < 5; i++) {
            sim_step(&s);
        }
        sim_free(&s);
    } else {
        CHECK(err[0] != '\0');
    }
}

static void test_damaged_bytes_never_crash(void)
{
    size_t size = 0;
    unsigned char *bytes = small_save(&size);
    CHECK(bytes != NULL);
    if (bytes == NULL) {
        return;
    }
    /* Every byte of the start of the file, where the sizes live. */
    for (size_t at = 0; at < 64 && at < size; at++) {
        try_damage(bytes, size, at, 0xFF);
    }
    /* Random bytes everywhere else: all bits, the top bit, a low bit. */
    Rng r;
    rng_seed(&r, 12345);
    const unsigned char masks[] = {0xFF, 0x80, 0x01, 0x7F};
    for (int i = 0; i < 1200; i++) {
        size_t at = (size_t)rng_below(&r, (uint32_t)size);
        try_damage(bytes, size, at, masks[rng_below(&r, 4)]);
    }
    free(bytes);
}

int main(void)
{
    test_round_trip_and_continue();
    test_chronicle_saved();
    test_settings_travel_with_the_file();
    test_random_brain_reopens();
    test_wrong_magic_and_version();
    test_mate_wish_survives_save();
    test_cut_short();
    test_extra_bytes();
    test_missing_file_and_bad_path();
    test_failed_write_keeps_old_file();
    test_save_over_existing_file();
    test_layout_is_as_documented();
    test_refuses_impossible_header();
    test_refuses_impossible_settings();
    test_refuses_impossible_layers();
    test_refuses_impossible_histories();
    test_refuses_impossible_records();
    test_dead_world_round_trip();
    test_refuses_impossible_creature();
    test_refuses_creature_off_soil();
    test_refuses_broken_free_list();
    test_refuses_two_on_one_cell();
    test_damaged_bytes_never_crash();
    remove(SAVE_FILE);
    remove(BROKEN_FILE);
    TEST_SUMMARY();
}
