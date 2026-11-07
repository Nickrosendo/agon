#include "test.h"

#include <string.h> /* memcmp, memset */

#include "tiles.h"
#include "world.h"  /* VEG_MAX, world_init, TERRAIN_ */

static int mask_bits(const uint16_t rows[16])
{
    int n = 0;
    for (int j = 0; j < 16; j++) {
        for (unsigned r = rows[j]; r != 0; r >>= 1) {
            n += (int)(r & 1u);
        }
    }
    return n;
}

static void test_same_inputs_same_mask(void)
{
    for (int kind = 0; kind < TILE_KINDS; kind++) {
        uint16_t a[16], b[16];
        tiles_build(a, kind, 2, 1, 16, 1);
        tiles_build(b, kind, 2, 1, 16, 1);
        CHECK(memcmp(a, b, sizeof(a)) == 0); /* real arrays: sizeof is 32 */
        CHECK(mask_bits(a) > 0);
    }
}

static void test_variant_is_stable_and_spread(void)
{
    int seen[TILE_VARIANTS] = {0};
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int v = tiles_variant(x, y);
            CHECK(v >= 0 && v < TILE_VARIANTS);
            CHECK(v == tiles_variant(x, y));
            if (v >= 0 && v < TILE_VARIANTS) {
                seen[v]++;
            }
        }
    }
    /* Each of the four about a quarter of the time, not merely once. */
    for (int v = 0; v < TILE_VARIANTS; v++) {
        CHECK(seen[v] > 64 * 64 / 8);
    }
}

static void test_plant_grows_with_density(void)
{
    for (int size = 8; size <= 16; size += 8) {
        for (int v = 0; v < TILE_VARIANTS; v++) {
            int before = -1;
            for (int d = 0; d <= VEG_MAX; d++) {
                uint16_t m[16];
                tiles_build(m, TILE_PLANT, v, d, size, 0);
                int n = mask_bits(m);
                CHECK(n > before);
                if (d == 0) {
                    CHECK(n == 0);
                }
                before = n;
            }
        }
    }
}

static void test_rain_frames_differ(void)
{
    uint16_t a[16], b[16];
    tiles_build(a, TILE_RAIN, 0, 0, 16, 0);
    tiles_build(b, TILE_RAIN, 0, 0, 16, 1);
    CHECK(memcmp(a, b, sizeof(a)) != 0);
}

static void test_marks_change_over_their_frames(void)
{
    const int kinds[2] = {TILE_SLASH, TILE_CRUMBS};
    for (int k = 0; k < 2; k++) {
        for (int size = 8; size <= 16; size += 8) {
            for (int f = 0; f < TILE_MARK_FRAMES; f++) {
                uint16_t a[16], b[16];
                tiles_build(a, kinds[k], 0, 0, size, f);
                tiles_build(b, kinds[k], 0, 0, size,
                            (f + 1) % TILE_MARK_FRAMES);
                CHECK(mask_bits(a) > 0);
                CHECK(memcmp(a, b, sizeof(a)) != 0);
            }
        }
    }
}

static void test_size_8_stays_in_tile(void)
{
    for (int kind = 0; kind < TILE_KINDS; kind++) {
        for (int v = 0; v < TILE_VARIANTS; v++) {
            for (int d = 0; d <= VEG_MAX; d++) {
                for (int f = 0; f < TILE_RAIN_FRAMES; f++) {
                    uint16_t m[16];
                    tiles_build(m, kind, v, d, 8, f);
                    for (int j = 0; j < 16; j++) {
                        CHECK(m[j] < 256);
                        if (j >= 8) {
                            CHECK(m[j] == 0);
                        }
                    }
                }
            }
        }
    }
}

/* ---- The ground's autotiles ---- */

/* A world from rows of letters: W deep water, S shallow, G soil, R rock. */
static bool make_map(World *w, const char *const *rows, int width, int height)
{
    if (!world_init(w, width, height)) {
        return false;
    }
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            char ch = rows[y][x];
            int t = ch == 'W' ? TERRAIN_WATER
                  : ch == 'S' ? TERRAIN_SHALLOW
                  : ch == 'R' ? TERRAIN_ROCK : TERRAIN_SOIL;
            w->terrain[y * width + x] = (uint8_t)t;
        }
    }
    return true;
}

static bool is_wet(int cls)
{
    return cls == CLASS_WATER || cls == CLASS_SHALLOW;
}

static void test_same_key_same_tile(void)
{
    static const uint32_t KEYS[3] = {0u, 0x1555555u & 0x1FFFFFu, 0x0ABCDEu};
    for (int size = 8; size <= 16; size += 8) {
        for (int k = 0; k < 3; k++) {
            uint16_t a[TILE_MAX * TILE_MAX], b[TILE_MAX * TILE_MAX];
            tiles_build_auto(a, KEYS[k], size);
            tiles_build_auto(b, KEYS[k], size);
            CHECK(memcmp(a, b, (size_t)(size * size) * sizeof(a[0])) == 0);
        }
    }
}

/* The hand-made map: all four classes, a lake of one cell at (3, 5), an
 * island of rock at (5, 7), and water across the wrapped edges. */
#define MAP_W 12
#define MAP_H 9
static const char *const MAP[MAP_H] = {
    "GGGGGSSGGWWW",
    "GGGGSSSGGWWW",
    "GGRGGSGGWWWW",
    "GRRGGGGGWWWW",
    "GGGGGGGSWWWW",
    "GGGWGGGGSWWW",
    "WWGGGGGGGSWW",
    "WSWGGRGGGGWW",
    "WWWWGGGGGGGW",
};

/* One picture of the whole map, every cell's tile built on its own and laid
 * beside the others, and each pixel's class: its source cell's class, read
 * from the map itself rather than from the tile's key. */
#define PIC_W (MAP_W * TILE_MAX)
#define PIC_H (MAP_H * TILE_MAX)
typedef struct {
    int size, w, h;
    uint16_t code[PIC_H][PIC_W];
    int cls[PIC_H][PIC_W];
} Picture;

static void assemble(Picture *p, const World *w, int size)
{
    p->size = size;
    p->w = MAP_W * size;
    p->h = MAP_H * size;
    for (int y = 0; y < MAP_H; y++) {
        for (int x = 0; x < MAP_W; x++) {
            uint16_t tile[TILE_MAX * TILE_MAX];
            tiles_build_auto(tile, tiles_key(w, x, y), size);
            for (int ly = 0; ly < size; ly++) {
                for (int lx = 0; lx < size; lx++) {
                    uint16_t code = tile[ly * size + lx];
                    int source = (int)TILE_SOURCE(code);
                    int dx = source % 3 - 1, dy = source / 3 - 1;
                    int cell = world_idx(w, x + dx, y + dy);
                    p->code[y * size + ly][x * size + lx] = code;
                    p->cls[y * size + ly][x * size + lx] =
                        tiles_class(w->terrain[cell]);
                }
            }
        }
    }
}

/* A pixel of the picture, wrapped round its edges like the world. */
static int pic_class(const Picture *p, int x, int y)
{
    x = (x % p->w + p->w) % p->w; /* % of a negative keeps the minus in C */
    y = (y % p->h + p->h) % p->h;
    return p->cls[y][x];
}

static void check_inner_pixels_own_cell(const Picture *p)
{
    int reach = p->size == 16 ? 4 : 2;
    for (int y = 0; y < p->h; y++) {
        for (int x = 0; x < p->w; x++) {
            int lx = x % p->size, ly = y % p->size;
            int border = lx;
            border = ly < border ? ly : border;
            border = p->size - 1 - lx < border ? p->size - 1 - lx : border;
            border = p->size - 1 - ly < border ? p->size - 1 - ly : border;
            if (border > reach) {
                CHECK(TILE_SOURCE(p->code[y][x]) == 4u);
            }
        }
    }
}

/* Down every column: under the last land pixel of a run, the water reads
 * bank, bank, bank dark, shadow, shadow (at size 8 bank, bank dark, shadow),
 * for as long as it stays water. Water with no land that close above has no
 * bank edge, and land never has one. Returns how many runs ended in the tile
 * above the water's own, so the caller can see that case was met. */
static int check_banks(const Picture *p)
{
    static const int BELOW16[5] = {EDGE_BANK, EDGE_BANK, EDGE_BANK_DARK,
                                   EDGE_SHADOW, EDGE_SHADOW};
    static const int BELOW8[3] = {EDGE_BANK, EDGE_BANK_DARK, EDGE_SHADOW};
    const int *below = p->size == 16 ? BELOW16 : BELOW8;
    int reach = p->size == 16 ? 5 : 3;
    int across_tiles = 0;
    for (int x = 0; x < p->w; x++) {
        for (int y = 0; y < p->h; y++) {
            int edge = (int)TILE_EDGE(p->code[y][x]);
            bool bank_edge = edge == EDGE_BANK || edge == EDGE_BANK_DARK ||
                             edge == EDGE_SHADOW;
            if (!is_wet(p->cls[y][x])) {
                CHECK(!bank_edge);
                continue;
            }
            int land_above = 0; /* how far up the nearest land is; 0: none */
            for (int d = 1; d <= reach && land_above == 0; d++) {
                if (!is_wet(pic_class(p, x, y - d))) {
                    land_above = d;
                }
            }
            if (land_above == 0) {
                CHECK(edge == EDGE_NONE);
            } else {
                CHECK(edge == below[land_above - 1]);
                if (land_above == 1 && y % p->size == 0) {
                    across_tiles++;
                }
            }
        }
    }
    return across_tiles;
}

/* Foam exactly on water with land among its eight neighbours, across tiles. */
static void check_foam(const Picture *p)
{
    for (int y = 0; y < p->h; y++) {
        for (int x = 0; x < p->w; x++) {
            bool near_land = false;
            for (int dy = -1; dy <= 1; dy++) {
                for (int dx = -1; dx <= 1; dx++) {
                    near_land |= !is_wet(pic_class(p, x + dx, y + dy));
                }
            }
            bool foam = (p->code[y][x] & TILE_FOAM) != 0;
            CHECK(foam == (is_wet(p->cls[y][x]) && near_land));
        }
    }
}

static void test_tiles_assemble_like_one_picture(void)
{
    World w;
    CHECK(make_map(&w, MAP, MAP_W, MAP_H));
    /* static: a picture of 165 kB is too large to be comfortable on the
     * stack, so it lives with the program's other fixed data instead. */
    static Picture p;
    for (int size = 8; size <= 16; size += 8) {
        assemble(&p, &w, size);
        check_inner_pixels_own_cell(&p);
        CHECK(check_banks(&p) > 0);
        check_foam(&p);
    }
    world_free(&w);
}

static void test_one_cell_lake_and_island(void)
{
    static const char *const LAKE[5] = {"GGGGG", "GGGGG", "GGWGG", "GGGGG",
                                        "GGGGG"};
    static const char *const ISLAND[5] = {"WWWWW", "WWWWW", "WWGWW", "WWWWW",
                                          "WWWWW"};
    World w;
    uint16_t tile[TILE_MAX * TILE_MAX];
    CHECK(make_map(&w, LAKE, 5, 5));
    tiles_build_auto(tile, tiles_key(&w, 2, 2), 16);
    int foam = 0, bank_on_top = 0;
    for (int i = 0; i < 16 * 16; i++) {
        bool own = TILE_SOURCE(tile[i]) == 4u;
        foam += own && (tile[i] & TILE_FOAM) != 0;
        bank_on_top += own && i / 16 < 8 && TILE_EDGE(tile[i]) == EDGE_BANK;
    }
    CHECK(foam > 0);
    CHECK(bank_on_top > 0);
    world_free(&w);

    CHECK(make_map(&w, ISLAND, 5, 5));
    int bank = 0, shadow = 0;
    /* Under the island: its own tile's lower part and the tile below it. */
    for (int below = 0; below <= 1; below++) {
        tiles_build_auto(tile, tiles_key(&w, 2, 2 + below), 16);
        for (int i = 0; i < 16 * 16; i++) {
            bank += TILE_EDGE(tile[i]) == EDGE_BANK;
            shadow += TILE_EDGE(tile[i]) == EDGE_SHADOW;
        }
    }
    CHECK(bank > 0);
    CHECK(shadow > 0);
    world_free(&w);
}

/* The class of key cell k (0..8, reading order). */
static int key_class(uint32_t key, int k)
{
    return (int)((key >> (3 + 2 * k)) & 3u);
}

static void test_wrapped_key(void)
{
    static const char *const SMALL[3] = {"WWWW", "WWWW", "WWWR"};
    World w;
    CHECK(make_map(&w, SMALL, 4, 3));
    uint32_t key = tiles_key(&w, 0, 0);
    CHECK(key_class(key, 0) == CLASS_ROCK);  /* north-west: (3, 2) */
    CHECK(key_class(key, 1) == CLASS_WATER); /* north: (0, 2) */
    CHECK(key_class(key, 4) == CLASS_WATER); /* the cell itself */
    CHECK((int)(key & 7u) == tiles_ground_variant(0, 0));
    /* And (3, 2) holds itself in the middle. */
    CHECK(key_class(tiles_key(&w, 3, 2), 4) == CLASS_ROCK);
    world_free(&w);
}

static void test_ground_variants_spread(void)
{
    int seen[TILE_GROUND_VARIANTS] = {0};
    for (int y = 0; y < 64; y++) {
        for (int x = 0; x < 64; x++) {
            int v = tiles_ground_variant(x, y);
            CHECK(v >= 0 && v < TILE_GROUND_VARIANTS);
            CHECK(v == tiles_ground_variant(x, y));
            if (v >= 0 && v < TILE_GROUND_VARIANTS) {
                seen[v]++;
            }
        }
    }
    for (int v = 0; v < TILE_GROUND_VARIANTS; v++) {
        CHECK(seen[v] > 64 * 64 / 16);
    }
}

/* Every key a map of only deep water and soil can make: each of the 9 cells
 * one or the other, with every variant. */
static void test_no_tile_writes_outside_itself(void)
{
    enum { GUARD = 8 };
    for (int size = 8; size <= 16; size += 8) {
        for (uint32_t cells = 0; cells < 512u; cells++) {
            uint32_t classes = 0;
            for (int k = 0; k < 9; k++) {
                uint32_t cls = (cells >> k) & 1u ? CLASS_SOIL : CLASS_WATER;
                classes |= cls << (3 + 2 * k);
            }
            for (uint32_t v = 0; v < TILE_GROUND_VARIANTS; v++) {
                uint16_t code[TILE_MAX * TILE_MAX + GUARD];
                for (int i = 0; i < TILE_MAX * TILE_MAX + GUARD; i++) {
                    code[i] = 0xBEEFu;
                }
                tiles_build_auto(code, classes | v, size);
                bool inside_ok = true, outside_ok = true;
                for (int i = 0; i < size * size; i++) {
                    inside_ok &= TILE_SOURCE(code[i]) <= 8u &&
                                 TILE_SHADE(code[i]) <= 2u &&
                                 TILE_EDGE(code[i]) <= EDGE_GRASS_SPECK &&
                                 (code[i] >> 11) == 0;
                }
                for (int i = size * size; i < TILE_MAX * TILE_MAX + GUARD; i++) {
                    outside_ok &= code[i] == 0xBEEFu;
                }
                /* One CHECK per tile rather than per pixel, or a fault would
                 * print thousands of lines. */
                CHECK(inside_ok);
                CHECK(outside_ok);
            }
        }
    }
}

static void test_cache_reuses_and_replaces(void)
{
    TileCache c;
    CHECK(tiles_cache_init(&c, 4));
    const TileEntry *e[5]; /* e[1] .. e[4]: key k in e[k] */
    for (uint32_t k = 1; k <= 4; k++) {
        e[k] = tiles_cache_get(&c, k << 3, 16);
        CHECK(e[k] != NULL);
    }
    CHECK(c.builds == 4);
    /* A hit: the same tile, nothing built. */
    CHECK(tiles_cache_get(&c, 1u << 3, 16) == e[1]);
    CHECK(c.builds == 4);
    /* The same key at the other size is another tile. */
    CHECK(tiles_cache_get(&c, 1u << 3, 8) != e[1]);
    CHECK(c.builds == 5);
    /* That replaced key 2, the one unused longest: 3, 4 and 1 are still kept. */
    CHECK(tiles_cache_get(&c, 3u << 3, 16) == e[3]);
    CHECK(tiles_cache_get(&c, 4u << 3, 16) == e[4]);
    CHECK(tiles_cache_get(&c, 1u << 3, 16) == e[1]);
    CHECK(c.builds == 5);
    const TileEntry *again = tiles_cache_get(&c, 2u << 3, 16);
    CHECK(c.builds == 6);
    CHECK(again != NULL && again->key == (2u << 3) && again->size == 16);
    /* That replaced the size 8 tile. A fifth key now replaces key 3, unused
     * the longest, and leaves key 1. */
    CHECK(tiles_cache_get(&c, 5u << 3, 16) != NULL);
    CHECK(c.builds == 7);
    CHECK(tiles_cache_get(&c, 1u << 3, 16) == e[1]);
    CHECK(c.builds == 7);
    tiles_cache_get(&c, 3u << 3, 16);
    CHECK(c.builds == 8);
    /* What it built is what tiles_build_auto builds. */
    uint16_t direct[TILE_MAX * TILE_MAX];
    tiles_build_auto(direct, 1u << 3, 16);
    CHECK(memcmp(direct, e[1]->code, sizeof(direct)) == 0);
    /* Reset forgets everything. */
    tiles_cache_reset(&c);
    tiles_cache_get(&c, 1u << 3, 16);
    CHECK(c.builds == 9);
    tiles_cache_free(&c);
    tiles_cache_free(&c); /* twice is safe */
}

static void test_bones_inside_tile(void)
{
    enum { GUARD = 8 };
    for (int size = 8; size <= 16; size += 8) {
        for (int v = 0; v < TILE_VARIANTS; v++) {
            uint8_t ink[TILE_MAX * TILE_MAX + GUARD];
            memset(ink, 0xAB, sizeof(ink));
            tiles_build_bones(ink, v, size);
            int count[4] = {0};
            bool valid = true, edge_clear = true;
            for (int i = 0; i < size * size; i++) {
                valid &= ink[i] <= 3u;
                if (ink[i] <= 3u) {
                    count[ink[i]]++;
                }
                int x = i % size, y = i / size;
                bool on_edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
                edge_clear &= !on_edge || ink[i] == 0;
            }
            CHECK(valid);
            CHECK(edge_clear);
            CHECK(count[1] > 0 && count[2] > 0 && count[3] > 0);
            for (int i = size * size; i < TILE_MAX * TILE_MAX; i++) {
                CHECK(ink[i] == 0);
            }
            for (int i = TILE_MAX * TILE_MAX; i < TILE_MAX * TILE_MAX + GUARD; i++) {
                CHECK(ink[i] == 0xAB);
            }
        }
    }
}

int main(void)
{
    test_same_inputs_same_mask();
    test_variant_is_stable_and_spread();
    test_plant_grows_with_density();
    test_rain_frames_differ();
    test_marks_change_over_their_frames();
    test_size_8_stays_in_tile();
    test_same_key_same_tile();
    test_tiles_assemble_like_one_picture();
    test_one_cell_lake_and_island();
    test_wrapped_key();
    test_ground_variants_spread();
    test_no_tile_writes_outside_itself();
    test_cache_reuses_and_replaces();
    test_bones_inside_tile();
    TEST_SUMMARY();
}
