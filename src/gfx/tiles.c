#include "tiles.h"

#include <math.h>   /* fabsf, floorf */
#include <stdlib.h> /* calloc, malloc, free */
#include <string.h> /* memset */

#include "gfx_hash.h"

/*
 * Mixes three small numbers into one 32-bit number, so nearby inputs give
 * unrelated outputs. Unsigned arithmetic on purpose: when an unsigned
 * multiplication overflows it wraps round, which C defines. The same on a
 * signed int would be undefined behaviour.
 */
static uint32_t hash3(uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t h = a * 374761393u + b * 668265263u + c * 2147483647u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

int tiles_variant(int x, int y)
{
    /* The top bits are the best mixed: >> 30 leaves 0..3. */
    return (int)(hash3((uint32_t)x, (uint32_t)y, 1u) >> 30);
}

static void set_pixel(uint16_t rows[16], int size, int i, int j)
{
    if (i >= 0 && i < size && j >= 0 && j < size) {
        rows[j] = (uint16_t)(rows[j] | (1u << (size - 1 - i)));
    }
}

/* A stem and a round crown, both growing with density; density 0 is bare.
 * The variant leans the crown a pixel to one side, so a meadow is not a
 * grid of identical trees. */
static void build_plant(uint16_t rows[16], int variant, int detail, int size)
{
    if (detail <= 0) {
        return;
    }
    float d = (float)detail / 4.0f;
    float lean = (float)(variant % 3 - 1) / (float)size;
    float crown_x = 0.5f + lean;
    float crown_y = 0.72f - 0.34f * d;
    float crown_r = detail == 1 ? 0.0f : 0.10f + 0.20f * d;
    float half_stem = 0.5f / (float)size + 0.001f;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            float u = ((float)i + 0.5f) / (float)size;
            float v = ((float)j + 0.5f) / (float)size;
            float dx = u - crown_x, dy = v - crown_y;
            int in_crown = dx * dx + dy * dy < crown_r * crown_r;
            int in_stem = fabsf(u - 0.5f) < half_stem && v > crown_y && v < 0.92f;
            int in_base = fabsf(v - 0.88f) < half_stem &&
                          fabsf(u - 0.5f) < 0.06f + 0.14f * d;
            if (in_crown || in_stem || in_base) {
                set_pixel(rows, size, i, j);
            }
        }
    }
}

/* A claw mark: a diagonal cross that flares and fades over its frames. */
static void build_slash(uint16_t rows[16], int size, int frame)
{
    /* How far each arm reaches from the middle, as a share of the tile. */
    static const float REACH[TILE_MARK_FRAMES] = {0.18f, 0.44f, 0.30f, 0.0f};
    float reach = REACH[frame % TILE_MARK_FRAMES];
    float line = 0.5f / (float)size + 0.01f;
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            float u = ((float)i + 0.5f) / (float)size - 0.5f;
            float v = ((float)j + 0.5f) / (float)size - 0.5f;
            if (fabsf(u) <= reach && fabsf(v) <= reach &&
                (fabsf(u - v) < line || fabsf(u + v) < line)) {
                set_pixel(rows, size, i, j);
            }
        }
    }
    /* The fourth frame's reach of 0 still leaves a single spark in the middle,
     * so the mark never vanishes while the fight goes on. */
    set_pixel(rows, size, size / 2, size / 2);
}

/* Three crumbs on arcs up and out from the middle; each frame moves them on. */
static void build_crumbs(uint16_t rows[16], int size, int frame)
{
    /* Whole-pixel steps in sixteenths of a tile, one list per crumb. */
    static const int STEP_X[3][TILE_MARK_FRAMES] = {
        {7, 5, 3, 2}, {8, 9, 10, 12}, {9, 7, 6, 5}};
    static const int STEP_Y[3][TILE_MARK_FRAMES] = {
        {9, 6, 4, 5}, {8, 5, 4, 6}, {10, 8, 7, 9}};
    int f = frame % TILE_MARK_FRAMES;
    for (int k = 0; k < 3; k++) {
        int x = STEP_X[k][f] * size / 16;
        int y = STEP_Y[k][f] * size / 16;
        set_pixel(rows, size, x, y);
        /* Two pixels wide at the larger size, or it would be hard to see. */
        if (size > 8) {
            set_pixel(rows, size, x + 1, y);
        }
    }
}

/* Short slanted streaks, placed by the variant, falling a step each frame. */
static void build_rain(uint16_t rows[16], int variant, int size, int frame)
{
    int fall = frame * size / TILE_RAIN_FRAMES;
    int length = size > 8 ? 3 : 2;
    for (uint32_t k = 0; k < 2u; k++) {
        int i = (int)(hash3((uint32_t)variant, k, 8u) % (uint32_t)size);
        int j = (int)(hash3((uint32_t)variant, k, 9u) % (uint32_t)size);
        for (int d = 0; d < length; d++) {
            /* Added to size first so that % never sees a negative number, whose
             * result would keep the minus sign in C. */
            set_pixel(rows, size, (i - d + size) % size, (j + fall + d) % size);
        }
    }
}

void tiles_build(uint16_t rows[16], int kind, int variant, int detail, int size,
                 int frame)
{
    /* sizeof(rows) would be wrong here: an array parameter is really a
     * pointer, and sizeof would give the pointer's size. Hence 16 * .... */
    memset(rows, 0, 16 * sizeof(rows[0]));
    switch (kind) {
    case TILE_PLANT: build_plant(rows, variant, detail, size); break;
    case TILE_SLASH: build_slash(rows, size, frame); break;
    case TILE_CRUMBS: build_crumbs(rows, size, frame); break;
    case TILE_RAIN: build_rain(rows, variant, size, frame); break;
    default: break;
    }
}

/* ---- Carrion ---- */

/* Whether the point (u, v), 0..1 across the tile, is on a bone: a line with a
 * knob at each end, tilted by the variant, and for even variants a short rib
 * below it. */
static bool on_bone(float u, float v, int variant, int size)
{
    float tilt = (float)(variant - 2) * 0.12f + 0.06f;
    float half = size > 8 ? 0.30f : 0.28f;
    float knob = size > 8 ? 0.10f : 0.13f;
    float line = 0.5f / (float)size + 0.01f;
    float x0 = 0.5f - half, y0 = 0.5f - tilt;
    float x1 = 0.5f + half, y1 = 0.5f + tilt;
    float along_v = y0 + (y1 - y0) * (u - x0) / (x1 - x0);
    bool in_line = u > x0 && u < x1 && fabsf(v - along_v) < line;
    float a = u - x0, b = v - y0, c = u - x1, e = v - y1;
    bool in_knob = a * a + b * b < knob * knob || c * c + e * e < knob * knob;
    bool in_rib = variant % 2 == 0 && u > 0.38f && u < 0.64f &&
                  fabsf(v - 0.78f) < line;
    return in_line || in_knob || in_rib;
}

void tiles_build_bones(uint8_t ink[TILE_MAX * TILE_MAX], int variant, int size)
{
    memset(ink, 0, TILE_MAX * TILE_MAX); /* uint8_t: one byte each */
    for (int j = 0; j < size; j++) {
        for (int i = 0; i < size; i++) {
            float u = ((float)i + 0.5f) / (float)size;
            float v = ((float)j + 0.5f) / (float)size;
            /* The stain: an ellipse whose edge is frayed by a hash. */
            float du = (u - 0.5f) / 0.38f, dv = (v - 0.6f) / 0.26f;
            float fray = gfx_hash(i, j, 40 + variant) * 0.3f;
            if (du * du + dv * dv < 1.0f - fray) {
                ink[j * size + i] = 3;
            }
            if (on_bone(u, v, variant, size)) {
                ink[j * size + i] = 1;
            }
        }
    }
    /* A bone's underside is its dark side: the lowest pixel of a part at
     * least two pixels tall. Going up the rows, so a pixel already darkened
     * below does not count as bone. A thin shaft stays light. */
    for (int j = size - 1; j >= 1; j--) {
        for (int i = 0; i < size; i++) {
            bool bone_below = j + 1 < size && ink[(j + 1) * size + i] != 0 &&
                              ink[(j + 1) * size + i] != 3;
            bool bone_above = ink[(j - 1) * size + i] == 1;
            if (ink[j * size + i] == 1 && bone_above && !bone_below) {
                ink[j * size + i] = 2;
            }
        }
    }
    /* The tile's outer ring stays empty, so carrion never touches the next cell. */
    for (int k = 0; k < size; k++) {
        ink[k] = 0;
        ink[(size - 1) * size + k] = 0;
        ink[k * size] = 0;
        ink[k * size + size - 1] = 0;
    }
}

/* ---- The ground as autotiles ---- */

int tiles_class(int terrain)
{
    switch (terrain) {
    case TERRAIN_WATER: return CLASS_WATER;
    case TERRAIN_SHALLOW: return CLASS_SHALLOW;
    case TERRAIN_ROCK: return CLASS_ROCK;
    default: return CLASS_SOIL;
    }
}

int tiles_ground_variant(int x, int y)
{
    /* The top bits are the best mixed: >> 29 leaves 0..7. */
    return (int)(hash3((uint32_t)x, (uint32_t)y, 2u) >> 29);
}

uint32_t tiles_key(const World *w, int x, int y)
{
    uint32_t key = (uint32_t)tiles_ground_variant(x, y);
    int k = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            /* world_idx wraps, so a cell on the edge sees the far side. */
            int cls = tiles_class(w->terrain[world_idx(w, x + dx, y + dy)]);
            key |= (uint32_t)cls << (3 + 2 * k);
            k++;
        }
    }
    return key;
}

/* The class of one of the key's nine cells, dx and dy each -1..1. */
static int key_class(uint32_t key, int dx, int dy)
{
    return tiles_key_class(key, (dy + 1) * 3 + (dx + 1));
}

int tiles_key_class(uint32_t key, int source)
{
    return (int)((key >> (3 + 2 * source)) & 3u);
}

static bool class_is_wet(int cls)
{
    return cls == CLASS_WATER || cls == CLASS_SHALLOW;
}

/* a / b rounded down. C's / rounds towards zero, so -1 / 16 is 0, not -1. */
static int floor_div(int a, int b)
{
    int q = a / b;
    return (a % b != 0 && (a < 0) != (b < 0)) ? q - 1 : q;
}

static int clamp1(int v)
{
    return v < -1 ? -1 : (v > 1 ? 1 : v);
}

void tiles_jitter(int size, int lx, int ly, int *jx, int *jy)
{
    /* At 16 pixels: blocks of 4 pixels, each moved up to 3.5 pixels either
     * way (v9:521). At 8 every distance is halved. */
    int shift = size >= 16 ? 2 : 1;
    float reach = 7.0f * (float)size / 16.0f;
    *jx = (int)floorf((gfx_hash(lx >> shift, ly >> shift, 7) - 0.5f) * reach);
    *jy = (int)floorf((gfx_hash(lx >> shift, ly >> shift, 9) - 0.5f) * reach);
}

/*
 * Which of the nine cells the point (px, py) shows, as dx and dy (each -1..1).
 * The point is in this tile's own pixels, but may lie a few pixels into a
 * neighbouring tile: it then uses that tile's jitter, so it resolves exactly
 * as the neighbour's own picture does there.
 */
static void resolve(int size, int px, int py, int *dx, int *dy)
{
    int tx = floor_div(px, size), ty = floor_div(py, size);
    int lx = px - tx * size, ly = py - ty * size;
    int jx, jy;
    tiles_jitter(size, lx, ly, &jx, &jy);
    /* Never further than the key's 3 by 3: a point at most 5 pixels outside
     * moved at most 4 cannot reach two cells away, so this only guards. */
    *dx = clamp1(tx + floor_div(lx + jx, size));
    *dy = clamp1(ty + floor_div(ly + jy, size));
}

static int class_at(uint32_t key, int size, int px, int py)
{
    int dx, dy;
    resolve(size, px, py, &dx, &dy);
    return key_class(key, dx, dy);
}

/* The edge of a water pixel: a bank under land just above, then its shadow.
 * Indexed by how far up the nearest land is (0: none within reach). */
static int bank_edge(uint32_t key, int size, int lx, int ly)
{
    static const int EDGE16[6] = {EDGE_NONE, EDGE_BANK, EDGE_BANK, EDGE_BANK_DARK,
                                  EDGE_SHADOW, EDGE_SHADOW};
    static const int EDGE8[4] = {EDGE_NONE, EDGE_BANK, EDGE_BANK_DARK, EDGE_SHADOW};
    const int *edge = size >= 16 ? EDGE16 : EDGE8;
    int reach = size >= 16 ? 5 : 3;
    for (int d = 1; d <= reach; d++) {
        if (!class_is_wet(class_at(key, size, lx, ly - d))) {
            return edge[d];
        }
    }
    return EDGE_NONE;
}

static bool touches_land(uint32_t key, int size, int lx, int ly)
{
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (!class_is_wet(class_at(key, size, lx + dx, ly + dy))) {
                return true;
            }
        }
    }
    return false;
}

/* The shade of a land pixel: fine noise over coarse noise (v9:536), cut in three. */
static int land_shade(int size, int lx, int ly, int variant)
{
    int fine = size >= 16 ? 1 : 0, coarse = size >= 16 ? 3 : 2;
    float n = gfx_hash(lx >> fine, ly >> fine, 3 + 16 * variant) * 0.6f +
              gfx_hash(lx >> coarse, ly >> coarse, 4 + 16 * variant) * 0.4f;
    return n < 0.33f ? 0 : (n < 0.7f ? 1 : 2);
}

static int land_edge(uint32_t key, int size, int lx, int ly, int cls, int variant)
{
    int edge = EDGE_NONE;
    if (cls == CLASS_ROCK) {
        /* Lit where the pixel up and to the left is not rock (v9:546). */
        if (class_at(key, size, lx - 1, ly - 1) != CLASS_ROCK) {
            edge = EDGE_ROCK_LIT;
        }
        if (gfx_hash(lx, ly, 8 + 16 * variant) > 0.92f) {
            edge = edge == EDGE_ROCK_LIT ? EDGE_ROCK_LIT_SPECK : EDGE_ROCK_SPECK;
        }
    } else if (cls == CLASS_SOIL && gfx_hash(lx, ly, 1 + 16 * variant) > 0.97f) {
        edge = EDGE_GRASS_SPECK;
    }
    return edge;
}

/* Whether the key holds both deep and shallow water: only then can a tile
 * have a border between the two. */
static bool has_both_waters(uint32_t key)
{
    bool deep = false, shallow = false;
    for (int k = 0; k < 9; k++) {
        int cls = tiles_key_class(key, k);
        deep |= cls == CLASS_WATER;
        shallow |= cls == CLASS_SHALLOW;
    }
    return deep && shallow;
}

/*
 * The source a water pixel of class cls borrows its colour from, plus 1, or 0
 * for its own. It looks for the other water (deep for shallow, shallow for
 * deep) up to reach pixels away; the nearer it is, the more likely the pixel
 * borrows its colour. "Likely" is an ordered 4 by 4 dither on the pixel's
 * place: a tile's corner is always a multiple of 4 pixels into the world, so
 * the pattern runs on across tiles without a seam.
 */
static unsigned water_alt(uint32_t key, int size, int lx, int ly, int cls)
{
    static const int BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6},
                                    {3, 11, 1, 9}, {15, 7, 13, 5}};
    int other = cls == CLASS_WATER ? CLASS_SHALLOW : CLASS_WATER;
    int reach = size >= 16 ? 3 : 2;
    for (int d = 1; d <= reach; d++) {
        /* The ring of pixels exactly d away (the larger of |dx| and |dy|). */
        for (int oy = -d; oy <= d; oy++) {
            for (int ox = -d; ox <= d; ox++) {
                bool on_ring = ox == -d || ox == d || oy == -d || oy == d;
                if (!on_ring) {
                    continue;
                }
                int dx, dy;
                resolve(size, lx + ox, ly + oy, &dx, &dy);
                if (key_class(key, dx, dy) != other) {
                    continue;
                }
                /* Chance (reach + 1 - d) / (2 (reach + 1)): 3/8, 2/8, 1/8 at 16. */
                int threshold = BAYER[ly & 3][lx & 3];
                bool borrow = threshold * 2 * (reach + 1) < 16 * (reach + 1 - d);
                return borrow ? (unsigned)((dy + 1) * 3 + (dx + 1)) + 1u : 0u;
            }
        }
    }
    return 0u;
}

void tiles_build_auto(uint16_t *code, uint32_t key, int size)
{
    int variant = (int)(key & 7u);
    bool both_waters = has_both_waters(key);
    for (int ly = 0; ly < size; ly++) {
        for (int lx = 0; lx < size; lx++) {
            int dx, dy;
            resolve(size, lx, ly, &dx, &dy);
            int cls = key_class(key, dx, dy);
            unsigned source = (unsigned)((dy + 1) * 3 + (dx + 1));
            unsigned shade = 0, edge, foam = 0, alt = 0;
            if (class_is_wet(cls)) {
                /* Water's shade moves with time, so scene works it out. */
                edge = (unsigned)bank_edge(key, size, lx, ly);
                /* A bank pixel can get foam too: the foam overlay (task 4)
                 * must let the bank's colour win there, as v9 does. */
                foam = touches_land(key, size, lx, ly) ? TILE_FOAM : 0u;
                alt = both_waters ? water_alt(key, size, lx, ly, cls) : 0u;
            } else {
                shade = (unsigned)land_shade(size, lx, ly, variant);
                edge = (unsigned)land_edge(key, size, lx, ly, cls, variant);
            }
            code[ly * size + lx] =
                (uint16_t)(source | shade << 4 | edge << 6 | foam | alt << 11);
        }
    }
}

/* ---- The cache of built tiles ---- */

bool tiles_cache_init(TileCache *c, int entries)
{
    *c = (TileCache){0};
    size_t n = (size_t)entries;
    /* calloc: zeroed memory, so every entry starts unused. */
    c->entries = calloc(n, sizeof(TileEntry));
    c->head = malloc(n * sizeof(int32_t));
    c->next = malloc(n * sizeof(int32_t));
    if (c->entries == NULL || c->head == NULL || c->next == NULL) {
        tiles_cache_free(c);
        return false;
    }
    c->capacity = entries;
    tiles_cache_reset(c);
    return true;
}

void tiles_cache_free(TileCache *c)
{
    /* free(NULL) does nothing, which is what makes a second call safe. */
    free(c->entries);
    free(c->head);
    free(c->next);
    long builds = c->builds;
    *c = (TileCache){0};
    c->builds = builds;
}

void tiles_cache_reset(TileCache *c)
{
    for (int i = 0; i < c->capacity; i++) {
        c->head[i] = -1;
        c->next[i] = -1;
        c->entries[i].used = false;
    }
    c->clock = 0;
}

/* Fills a built tile's looks and inks (see TileEntry). */
static void find_looks(TileEntry *t, uint32_t key)
{
    uint16_t first_slot[TILE_MAX * TILE_MAX]; /* of each look */
    int slots = 0;
    t->looks = 0;
    for (int i = 0; i < t->size * t->size; i++) {
        bool wet = class_is_wet(tiles_key_class(key, (int)TILE_SOURCE(t->code[i])));
        uint16_t look = (uint16_t)((t->code[i] & ~TILE_FOAM) | (wet ? TILE_INK_WET : 0u));
        int k = 0;
        /* A plain search: a tile has a few dozen looks at most. */
        while (k < t->looks && t->look[k] != look) {
            k++;
        }
        if (k == t->looks) {
            t->look[k] = look;
            first_slot[k] = (uint16_t)slots;
            slots += wet ? 3 : 1;
            t->looks++;
        }
        t->ink[i] = (uint16_t)(first_slot[k] | (wet ? TILE_INK_WET : 0u));
    }
}

/* Which bucket a key and size live in: one per entry, so chains stay short. */
static int bucket_of(const TileCache *c, uint32_t key, int size)
{
    return (int)(gfx_mix(key * 2u + (size >= 16 ? 1u : 0u)) % (uint32_t)c->capacity);
}

/* An unused entry if there is one, else the one unused longest. */
static int pick_victim(const TileCache *c)
{
    int best = 0;
    for (int i = 0; i < c->capacity; i++) {
        const TileEntry *e = &c->entries[i];
        if (!e->used) {
            return i;
        }
        if (e->last_used < c->entries[best].last_used) {
            best = i;
        }
    }
    return best;
}

/* Takes entry e out of its bucket's chain. */
static void unlink_entry(TileCache *c, int e)
{
    const TileEntry *t = &c->entries[e];
    /* A pointer to the link that points at e, so the first entry of a chain
     * and any later one are unlinked the same way. */
    int32_t *link = &c->head[bucket_of(c, t->key, t->size)];
    while (*link != -1 && *link != e) {
        link = &c->next[*link];
    }
    if (*link == e) {
        *link = c->next[e];
    }
    c->next[e] = -1;
}

const TileEntry *tiles_cache_get(TileCache *c, uint32_t key, int size)
{
    if (c->capacity <= 0) {
        return NULL;
    }
    int b = bucket_of(c, key, size);
    for (int32_t e = c->head[b]; e != -1; e = c->next[e]) {
        TileEntry *t = &c->entries[e];
        if (t->key == key && t->size == size) {
            t->last_used = ++c->clock;
            return t;
        }
    }
    int e = pick_victim(c);
    if (c->entries[e].used) {
        unlink_entry(c, e);
    }
    TileEntry *t = &c->entries[e];
    t->key = key;
    t->size = size;
    t->used = true;
    t->last_used = ++c->clock;
    tiles_build_auto(t->code, key, size);
    t->has_foam = false;
    t->has_wet = false;
    for (int i = 0; i < size * size; i++) {
        t->has_foam |= (t->code[i] & TILE_FOAM) != 0;
        t->has_wet |= class_is_wet(tiles_key_class(key, (int)TILE_SOURCE(t->code[i])));
    }
    find_looks(t, key);
    c->next[e] = c->head[b];
    c->head[b] = e;
    c->builds++;
    return t;
}
