#include "worldcode.h"

#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* malloc, realloc, free */
#include <string.h> /* memcpy, memcmp, strlen, strncmp, strstr, strcspn */

#include "genome.h"  /* GENE_TRAITS */
#include "options.h" /* OPTIONS_MAX_SIDE, OPTIONS_MAX_TICKS */
#include "weather.h" /* WEATHER_COUNT */

/* More actions than anyone gives by hand, and few enough that a damaged count
 * cannot ask for much memory (the same limit as a save file's). */
#define CODE_ACTIONS_MAX (1u << 20)

/* ---- the 64 characters ----
 *
 * Base64 writes three bytes (24 bits) as four characters of six bits each.
 * This alphabet is the "URL-safe" one: '-' and '_' in place of '+' and '/',
 * so a code survives being put in a web address or a chat that turns '/' into
 * a link. No '=' padding at the end: the length inside the code says where
 * the bytes stop.
 */
static const char ALPHABET[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

/* A character's six bits, or -1 for one that is not in the alphabet. A switch
 * on ranges rather than a search of ALPHABET: the letters and digits are in
 * order in every character set agon will meet, so c - 'A' is the letter's
 * place. */
static int six_bits(char c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '-') {
        return 62;
    }
    if (c == '_') {
        return 63;
    }
    return -1;
}

static bool is_blank(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

/* FNV-1a, 32 bits: each byte is mixed in by xor and a multiply. A wrong
 * character changes the result with all but a one in four billion chance. The
 * multiply is on uint32_t, which wraps round at 2^32 by definition; on a signed
 * int it would be undefined behaviour. */
static uint32_t fingerprint(const unsigned char *bytes, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) {
        h ^= bytes[i];
        h *= 16777619u;
    }
    return h;
}

/* ---- writing bytes ----
 *
 * The same sticky flag as the save file's Writer (save.c): the first failure
 * clears ok, later puts do nothing, and the caller checks once at the end.
 */
typedef struct {
    unsigned char *buf;
    size_t len, cap;
    bool ok;
} Bytes;

static void put_bytes(Bytes *b, const void *data, size_t n)
{
    if (!b->ok) {
        return;
    }
    if (b->cap - b->len < n) {
        size_t bigger = b->cap == 0 ? 256 : b->cap;
        while (bigger - b->len < n) {
            bigger *= 2;
        }
        unsigned char *grown = realloc(b->buf, bigger);
        if (grown == NULL) {
            b->ok = false; /* b->buf still holds the old block */
            return;
        }
        b->buf = grown;
        b->cap = bigger;
    }
    memcpy(b->buf + b->len, data, n);
    b->len += n;
}

/* Lowest byte first, spelled out with shifts, as in save.c, so a code made on
 * one machine opens on any other. */
static void put_u8(Bytes *b, unsigned v)
{
    unsigned char c = (unsigned char)(v & 0xFFu);
    put_bytes(b, &c, 1);
}

static void put_u16(Bytes *b, unsigned v)
{
    put_u8(b, v);
    put_u8(b, v >> 8);
}

static void put_u32(Bytes *b, uint32_t v)
{
    put_u16(b, v & 0xFFFFu);
    put_u16(b, v >> 16);
}

static void put_u64(Bytes *b, uint64_t v)
{
    put_u32(b, (uint32_t)(v & 0xFFFFFFFFu));
    put_u32(b, (uint32_t)(v >> 32));
}

/* A float's four bytes as a uint32_t. memcpy is the defined way to look at
 * one type's bits as another's; casting the pointer (*(uint32_t *)&f) breaks
 * C's rule that an object is read only through its own type, and an
 * optimising compiler may then read stale bits. */
static void put_float(Bytes *b, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    put_u32(b, bits);
}

/* The settings that differ from the defaults: their count, then row and value.
 * The count goes first, so the rows are gone over twice. Floats are compared
 * by their bits, not with ==: a value read back from a file can be equal as a
 * number and still be another float (0.0 and -0.0), and the code must carry
 * exactly what the world was built with. */
static bool setting_differs(const Settings *a, const Settings *b,
                            const SettingInfo *info)
{
    /* settings_int and friends take a non-const Settings, so the casts drop the
     * const; nothing is written through them here. */
    Settings *x = (Settings *)a, *y = (Settings *)b;
    switch (info->type) {
    case SETTING_INT: return *settings_int(x, info) != *settings_int(y, info);
    case SETTING_FLOAT:
        return memcmp(settings_float(x, info), settings_float(y, info),
                      sizeof(float)) != 0;
    case SETTING_TEXT:
        return strcmp(settings_text(x, info), settings_text(y, info)) != 0;
    }
    return false;
}

static void put_settings(Bytes *b, const Settings *set)
{
    Settings plain = settings_default();
    unsigned changed = 0;
    for (int i = 0; i < settings_count(); i++) {
        changed += setting_differs(set, &plain, settings_info(i)) ? 1u : 0u;
    }
    put_u16(b, changed);
    Settings *mine = (Settings *)set; /* read only, as above */
    for (int i = 0; i < settings_count(); i++) {
        const SettingInfo *info = settings_info(i);
        if (!setting_differs(set, &plain, info)) {
            continue;
        }
        put_u16(b, (unsigned)i);
        if (info->type == SETTING_INT) {
            /* A negative int becomes a large uint32_t, which is defined (it
             * wraps modulo 2^32); reading converts it back. */
            put_u32(b, (uint32_t)*settings_int(mine, info));
        } else if (info->type == SETTING_FLOAT) {
            put_float(b, *settings_float(mine, info));
        } else {
            const char *text = settings_text(mine, info);
            size_t n = strlen(text);
            put_u8(b, (unsigned)n); /* below BRAIN_NAME_MAX, so one byte */
            put_bytes(b, text, n);
        }
    }
}

static void put_actions(Bytes *b, const ActionLog *log)
{
    put_u32(b, (uint32_t)log->count);
    for (int i = 0; i < log->count; i++) {
        const PlayerAction *a = &log->items[i];
        put_u64(b, a->tick);
        put_u8(b, a->kind);
        put_u8(b, a->gene);
        put_u8(b, a->weather);
        put_u32(b, a->creature);
        put_float(b, a->value);
    }
}

/* Base64 of n bytes into a new string after the prefix. Every three bytes
 * become four characters; a last one or two bytes become two or three. */
static char *to_text(const unsigned char *bytes, size_t n)
{
    size_t prefix = strlen(WORLDCODE_PREFIX);
    /* (n + 2) / 3 groups of four, rounding up, and the '\0'. */
    char *text = malloc(prefix + (n + 2) / 3 * 4 + 1);
    if (text == NULL) {
        return NULL;
    }
    memcpy(text, WORLDCODE_PREFIX, prefix);
    char *out = text + prefix;
    for (size_t i = 0; i < n; i += 3) {
        /* Up to three bytes side by side in one 24-bit number, a missing
         * byte counting as 0. */
        uint32_t group = (uint32_t)bytes[i] << 16;
        if (i + 1 < n) {
            group |= (uint32_t)bytes[i + 1] << 8;
        }
        if (i + 2 < n) {
            group |= bytes[i + 2];
        }
        size_t chars = n - i >= 3 ? 4 : n - i + 1;
        for (size_t k = 0; k < chars; k++) {
            /* The six bits at 18, 12, 6 and 0. */
            *out++ = ALPHABET[(group >> (18 - 6 * k)) & 0x3Fu];
        }
    }
    *out = '\0';
    return text;
}

bool worldcode_write(const Sim *s, char **text)
{
    return worldcode_write_as(s, AGON_RULES, text);
}

bool worldcode_write_as(const Sim *s, unsigned rules, char **text)
{
    Bytes b = {.ok = true};
    put_u32(&b, 0); /* the length, filled in at the end */
    put_u16(&b, rules);
    put_u64(&b, s->seed);
    put_u16(&b, (unsigned)s->world.w);
    put_u16(&b, (unsigned)s->world.h);
    put_u64(&b, s->tick);
    put_settings(&b, &s->set);
    put_actions(&b, &s->actions);
    if (b.ok) {
        /* Now the length is known: the bytes after the first four. */
        uint32_t n = (uint32_t)(b.len - 4);
        for (int k = 0; k < 4; k++) {
            b.buf[k] = (unsigned char)((n >> (8 * k)) & 0xFFu);
        }
        put_u32(&b, fingerprint(b.buf, b.len));
    }
    *text = b.ok ? to_text(b.buf, b.len) : NULL;
    free(b.buf);
    return *text != NULL;
}

/* ---- reading ---- */

/* Decodes count characters of text (those after the prefix) into a new block
 * of *n bytes. -1 for
 * a character not in the alphabet, -2 for no memory, 0 when fine. Blanks are
 * skipped. A last group of one character is impossible (it would hold six bits,
 * less than a byte), so it is left out: the length check then finds the code
 * short. */
static int from_text(const char *text, size_t count, unsigned char **bytes,
                     size_t *n)
{
    size_t chars = 0;
    for (size_t i = 0; i < count; i++) {
        if (is_blank(text[i])) {
            continue;
        }
        if (six_bits(text[i]) < 0) {
            return -1;
        }
        chars++;
    }
    /* Three bytes per four characters, rounded up; at least 1 so malloc(0),
     * which may return NULL, never happens. */
    unsigned char *out = malloc(chars / 4 * 3 + 3);
    if (out == NULL) {
        return -2;
    }
    size_t len = 0;
    uint32_t group = 0;
    int have = 0; /* characters in group */
    for (size_t i = 0; i < count; i++) {
        if (is_blank(text[i])) {
            continue;
        }
        group = group << 6 | (uint32_t)six_bits(text[i]);
        have++;
        if (have == 4) {
            out[len++] = (unsigned char)(group >> 16);
            out[len++] = (unsigned char)(group >> 8 & 0xFFu);
            out[len++] = (unsigned char)(group & 0xFFu);
            group = 0;
            have = 0;
        }
    }
    if (have == 2) { /* 12 bits: one byte and four spare bits */
        out[len++] = (unsigned char)(group >> 4);
    } else if (have == 3) { /* 18 bits: two bytes and two spare */
        out[len++] = (unsigned char)(group >> 10);
        out[len++] = (unsigned char)(group >> 2 & 0xFFu);
    }
    *bytes = out;
    *n = len;
    return 0;
}

/* A cursor over the decoded bytes. Running past the end sets short_read and
 * every later get gives 0, the sticky flag again. The length and the check are
 * tested before anything else is read, so a short read here means a value
 * pointed past the end, which is a damaged code. */
typedef struct {
    const unsigned char *bytes;
    size_t len, at;
    bool short_read;
} Cursor;

static unsigned get_u8(Cursor *c)
{
    if (c->at >= c->len) {
        c->short_read = true;
        return 0;
    }
    return c->bytes[c->at++];
}

static unsigned get_u16(Cursor *c)
{
    unsigned lo = get_u8(c);
    return lo | get_u8(c) << 8;
}

static uint32_t get_u32(Cursor *c)
{
    uint32_t lo = get_u16(c);
    return lo | (uint32_t)get_u16(c) << 16;
}

static uint64_t get_u64(Cursor *c)
{
    uint64_t lo = get_u32(c);
    return lo | (uint64_t)get_u32(c) << 32;
}

static float get_float(Cursor *c)
{
    uint32_t bits = get_u32(c);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

/* One setting from the code into *set. False when the row or value is not one
 * this agon allows. A float is checked with !(v >= min && v <= max): every
 * comparison with NaN (not a number) is false, so this refuses NaN too, where
 * (v < min || v > max) would let it through. */
static bool get_setting(Cursor *c, Settings *set, int row)
{
    const SettingInfo *info = settings_info(row);
    if (info->type == SETTING_INT) {
        /* Back from uint32_t to int: a value above INT_MAX converts in an
         * implementation-defined way, which on every compiler agon meets is
         * the wrap that put_u32 did. */
        int v = (int)get_u32(c);
        if (!(v >= info->min && v <= info->max)) {
            return false;
        }
        *settings_int(set, info) = v;
    } else if (info->type == SETTING_FLOAT) {
        float v = get_float(c);
        if (!(v >= info->min && v <= info->max)) {
            return false;
        }
        *settings_float(set, info) = v;
    } else {
        unsigned n = get_u8(c);
        if (n >= BRAIN_NAME_MAX) {
            return false;
        }
        char *text = settings_text(set, info);
        for (unsigned k = 0; k < n; k++) {
            text[k] = (char)get_u8(c);
        }
        text[n] = '\0';
    }
    return !c->short_read;
}

static bool get_settings(Cursor *c, Settings *set)
{
    *set = settings_default();
    unsigned changed = get_u16(c);
    int last = -1;
    for (unsigned i = 0; i < changed; i++) {
        int row = (int)get_u16(c);
        /* Rows rise: a row twice, or one past the table, is a damaged code. */
        if (c->short_read || row <= last || row >= settings_count() ||
            !get_setting(c, set, row)) {
            return false;
        }
        last = row;
    }
    char ignored[128];
    return settings_check(set, ignored, sizeof(ignored));
}

static bool get_actions(Cursor *c, ActionLog *log, uint64_t until)
{
    uint32_t count = get_u32(c);
    if (count > CODE_ACTIONS_MAX) {
        return false;
    }
    for (uint32_t i = 0; i < count; i++) {
        PlayerAction a = {0};
        a.tick = get_u64(c);
        a.kind = (uint8_t)get_u8(c);
        a.gene = (uint8_t)get_u8(c);
        a.weather = (uint8_t)get_u8(c);
        a.creature = get_u32(c);
        a.value = get_float(c);
        /* An action may wait at the tick reached (made, not yet applied), not
         * past it. actions_add refuses one out of order. */
        if (c->short_read || a.kind >= ACTION_KIND_COUNT ||
            a.gene >= GENE_TRAITS || a.weather >= WEATHER_COUNT ||
            !(a.value >= 0.0f && a.value <= 1.0f) || a.tick > until ||
            !actions_add(log, a)) {
            return false;
        }
    }
    return true;
}

/* The checks before any value is believed: the length, then the check. */
static bool sound(const unsigned char *bytes, size_t n, char *err, size_t len)
{
    if (n < 4) {
        snprintf(err, len, "the world code is cut short: copy all of it");
        return false;
    }
    uint32_t body = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
                    (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    /* Written as subtractions, so a huge length in a damaged code cannot wrap
     * round: n - 4 is safe because n >= 4. */
    if (body > n - 4 || n - 4 - body < 4) {
        snprintf(err, len, "the world code is cut short: copy all of it");
        return false;
    }
    size_t checked = 4 + (size_t)body;
    uint32_t want = (uint32_t)bytes[checked] | (uint32_t)bytes[checked + 1] << 8 |
                    (uint32_t)bytes[checked + 2] << 16 |
                    (uint32_t)bytes[checked + 3] << 24;
    if (fingerprint(bytes, checked) != want) {
        snprintf(err, len, "the world code has a typo in it");
        return false;
    }
    return true;
}

static bool parse(const unsigned char *bytes, size_t n, WorldCode *code,
                  char *err, size_t len)
{
    if (!sound(bytes, n, err, len)) {
        return false;
    }
    Cursor c = {.bytes = bytes, .len = n - 4, .at = 4}; /* the check left out */
    unsigned rules = get_u16(&c);
    if (rules != AGON_RULES) {
        snprintf(err, len, "this world was made with rules %u; this agon has "
                           "rules %d", rules, AGON_RULES);
        return false;
    }
    code->seed = get_u64(&c);
    code->w = (int)get_u16(&c);
    code->h = (int)get_u16(&c);
    code->until = get_u64(&c);
    if (code->w < 1 || code->h < 1 || code->w > OPTIONS_MAX_SIDE ||
        code->h > OPTIONS_MAX_SIDE || code->until > OPTIONS_MAX_TICKS) {
        snprintf(err, len, "the world code asks for a world this agon cannot "
                           "make");
        return false;
    }
    if (!get_settings(&c, &code->set)) {
        snprintf(err, len, "the world code has settings this agon does not "
                           "allow");
        return false;
    }
    if (!get_actions(&c, &code->log, code->until)) {
        snprintf(err, len, "the world code has actions this agon cannot replay");
        return false;
    }
    if (c.short_read || c.at != c.len) {
        snprintf(err, len, "the world code is damaged");
        return false;
    }
    return true;
}

/*
 * Where a code's characters start in text (just after the prefix), with how
 * many of them there are in *count; NULL when text holds no code.
 *
 * A bare code: blanks first are skipped, and it runs to the end of the text.
 * A link to the browser page ("https://.../agon/?code=agon1:..."): the code
 * follows "code=" and runs up to the next '&' (the link's next field) or '#'
 * (a place on the page), so the rest of the address is not read as a typo.
 * Some programs write the ':' in an address as "%3A", the percent form of its
 * character number (0x3A); that prefix counts too.
 */
static const char *find_code(const char *text, size_t *count)
{
    size_t prefix = strlen(WORLDCODE_PREFIX);
    const char *bare = text;
    while (is_blank(*bare)) {
        bare++;
    }
    if (strncmp(bare, WORLDCODE_PREFIX, prefix) == 0) {
        *count = strlen(bare + prefix);
        return bare + prefix;
    }
    const char *field = strstr(text, WORLDCODE_LINK_FIELD);
    if (field == NULL) {
        return NULL;
    }
    const char *at = field + strlen(WORLDCODE_LINK_FIELD);
    const char *start = NULL;
    if (strncmp(at, WORLDCODE_PREFIX, prefix) == 0) {
        start = at + prefix;
    } else if (strncmp(at, WORLDCODE_PREFIX, prefix - 1) == 0 &&
               at[prefix - 1] == '%' && at[prefix] == '3' &&
               (at[prefix + 1] == 'A' || at[prefix + 1] == 'a')) {
        /* prefix - 1: every character but the ':', which is the 3 after */
        start = at + prefix + 2;
    } else {
        return NULL;
    }
    /* strcspn: how many characters from start before the first of "&#" (or
     * the end, '\0'). */
    *count = strcspn(start, "&#");
    return start;
}

bool worldcode_read(const char *text, WorldCode *code, char *err, size_t len)
{
    *code = (WorldCode){0};
    size_t count = 0;
    const char *start = find_code(text, &count);
    if (start == NULL) {
        snprintf(err, len, "that is not an agon world code (it starts with "
                           "%s)", WORLDCODE_PREFIX);
        return false;
    }
    unsigned char *bytes = NULL;
    size_t n = 0;
    int got = from_text(start, count, &bytes, &n);
    if (got == -1) {
        snprintf(err, len, "the world code has a typo in it");
        return false;
    }
    if (got == -2) {
        snprintf(err, len, "no memory left to read the world code");
        return false;
    }
    bool ok = parse(bytes, n, code, err, len);
    free(bytes);
    if (!ok) {
        worldcode_free(code); /* the log may have taken memory: none is kept */
    }
    return ok;
}

void worldcode_free(WorldCode *code)
{
    actions_free(&code->log);
    *code = (WorldCode){0};
}

bool worldcode_start(Sim *s, const WorldCode *code, char *err, size_t len)
{
    if (!sim_init(s, &code->set, code->seed, code->w, code->h)) {
        snprintf(err, len, "could not build the world: no memory, or its "
                           "settings cannot make one");
        return false;
    }
    /* sim_init left the log empty; the code's entries go in, in order, with
     * next at the first, since nothing has happened yet. */
    for (int i = 0; i < code->log.count; i++) {
        if (!actions_add(&s->actions, code->log.items[i])) {
            sim_free(s);
            snprintf(err, len, "no memory left for the world's actions");
            return false;
        }
    }
    actions_seek(&s->actions, 0);
    return true;
}
