#include "scene.h"

#include <math.h>   /* floorf, sinf */
#include <stdio.h>  /* snprintf */
#include <stdlib.h> /* free, malloc, realloc */
#include <string.h> /* memcmp, memcpy, strlen */

#include "app.h"
#include "chronicle.h" /* name_from_id */
#include "climate.h" /* climate_region, climate_region_hue */
#include "flora.h"   /* plant_gives_shade, plant_is_bare */
#include "gfx_hash.h"
#include "palette.h"
#include "years.h"   /* years_season_of */

/* How many autotiles the scene keeps. Real maps use a few hundred keys. */
#define SCENE_TILE_CACHE 2048
#define TWO_PI 6.2831853f
/* How long one frame of a fight's or a meal's mark lasts. */
#define MARK_FRAME_MS 110u

/* The near-black behind every symbol; outside the world it is true black. */
static const Colour BACKGROUND = {6, 6, 8};
/* The second colour of a creature: teeth and spots. */
static const Colour WHITE = {245, 240, 235};
/* Bones; and the colour of carrion: a little is pale bone, a lot dark red. */
static const Colour BONE = {196, 176, 150};
/* The flash of a fight: brighter than anything else in the world. */
static const Colour SLASH = {255, 70, 50};
/* Crumbs of meat. Crumbs of plant are the plant's own colour, lightened, or
 * they would vanish against the tree they come from. */
static const Colour MEAT_CRUMB = {255, 110, 100};
/* A cut not healed yet: a pale pink, to be seen on any family's colour. */
static const Colour SCAR = {255, 170, 160};

/* How far a region's colour tints a cell's ground colours, and the flat colour
 * of a cell at the furthest zoom. Small, so the ground stays ground. */
#define REGION_TINT 0.14f
#define REGION_TINT_FLAT 0.12f
/* The pale saturation plant-eaters are drawn in, so a body that matches its
 * region looks like that region's tint, only brighter. */
#define REGION_SATURATION 0.55f

static uint32_t to_pixel(Colour c)
{
    return canvas_rgb(c.r, c.g, c.b);
}

/* The whole canvas, for drawing that only the canvas's edges need to stop. */
static CanvasClip whole(const Canvas *c)
{
    return (CanvasClip){0, 0, c->w, c->h};
}

/* Marks every kept cell colour as unknown: after a new world, or when the
 * blocks were just allocated and hold nothing yet. */
static void forget_cell_colours(Scene *sc)
{
    for (int i = 0; i < sc->cell_ramps_capacity; i++) {
        sc->cell_pixels[i].known = false;
    }
}

/* Which of the SCENE_TREES pictures a tree uses. size_level 0 is the 16-pixel
 * size, 1 the 8-pixel one; density 1..VEG_MAX; sway -1..1. */
static int tree_at(int size_level, int density, bool big, bool bare, int variant,
                   int sway)
{
    int k = size_level * VEG_MAX + (density - 1);
    k = k * 2 + (big ? 1 : 0);
    k = k * 2 + (bare ? 1 : 0);
    k = k * TILE_VARIANTS + variant;
    return k * 3 + (sway + 1);
}

/* Every tree picture once, and the box its ink fills. False when memory runs
 * out. */
static bool build_trees(Scene *sc)
{
    sc->trees = malloc((size_t)SCENE_TREES * sizeof(TreePicture));
    if (sc->trees == NULL) {
        return false;
    }
    for (int z = 0; z < 2; z++) {
        for (int d = 1; d <= VEG_MAX; d++) {
            for (int big = 0; big <= 1; big++) {
                for (int bare = 0; bare <= 1; bare++) {
                    for (int v = 0; v < TILE_VARIANTS; v++) {
                        for (int sway = -1; sway <= 1; sway++) {
                            int k = tree_at(z, d, big == 1, bare == 1, v, sway);
                            tiles_build_tree(&sc->trees[k], d, big == 1, bare == 1,
                                             v, sway, z == 0 ? 16 : 8);
                        }
                    }
                }
            }
        }
    }
    for (int k = 0; k < SCENE_TREES; k++) {
        TreeBox b = {TREE_W, TREE_H, -1, -1};
        for (int i = 0; i < TREE_W * TREE_H; i++) {
            if (sc->trees[k].ink[i] != TREE_NONE) {
                int x = i % TREE_W, y = i / TREE_W;
                /* The casts: int8_t holds -128..127, and 0..TREE_H fits. */
                b.left = (int8_t)(x < b.left ? x : b.left);
                b.top = (int8_t)(y < b.top ? y : b.top);
                b.right = (int8_t)(x > b.right ? x : b.right);
                b.bottom = (int8_t)(y > b.bottom ? y : b.bottom);
            }
        }
        sc->tree_box[k] = b;
    }
    return true;
}

bool scene_init(Scene *sc, const Sim *s)
{
    if (!sprite_cache_init(&sc->sprites, s->pool.capacity)) {
        return false;
    }
    if (!anim_init(&sc->anim, s->pool.capacity)) {
        sprite_cache_free(&sc->sprites);
        return false;
    }
    if (!tiles_cache_init(&sc->tiles, SCENE_TILE_CACHE)) {
        sprite_cache_free(&sc->sprites);
        anim_free(&sc->anim);
        return false;
    }
    sc->poses = malloc((size_t)s->pool.capacity * sizeof(AnimPose));
    /* Sized by the first frame. An empty canvas allocates nothing, so this
     * cannot fail today; checked anyway, like every other allocation. */
    if (sc->poses == NULL || !build_trees(sc) || !canvas_init(&sc->ground, 0, 0)) {
        free(sc->poses); /* free(NULL) does nothing */
        free(sc->trees);
        sprite_cache_free(&sc->sprites);
        anim_free(&sc->anim);
        tiles_cache_free(&sc->tiles);
        return false;
    }
    sc->ground_valid = false;
    sc->ground_tiled = false;
    sc->ground_colours_pending = false;
    sc->tiles_level = -1;
    /* Allocated by the first frame that draws tiles. */
    sc->cell_ramps = NULL;
    sc->cell_pixels = NULL;
    sc->cell_cover = NULL;
    sc->cell_dirty = NULL;
    sc->cell_ramps_capacity = 0;
    sc->keys = NULL;
    sc->keys_terrain = NULL;
    sc->keys_capacity = 0;
    sc->keys_valid = false;
    sc->items = NULL;
    sc->items_spare = NULL;
    sc->items_capacity = 0;
    for (int i = 0; i < SCENE_SINE_STEPS; i++) {
        sc->sine[i] = sinf((float)i * TWO_PI / (float)SCENE_SINE_STEPS);
    }
    /* Every symbol once, here, so drawing a frame only copies bits. */
    for (int z = 0; z < 2; z++) {
        int size = z == 0 ? 16 : 8;
        for (int f = 0; f < TILE_MARK_FRAMES; f++) {
            tiles_build(sc->slash[z][f], TILE_SLASH, 0, size, f);
            tiles_build(sc->crumbs[z][f], TILE_CRUMBS, 0, size, f);
        }
        for (int v = 0; v < TILE_VARIANTS; v++) {
            for (int f = 0; f < TILE_RAIN_FRAMES; f++) {
                tiles_build(sc->rain[z][v][f], TILE_RAIN, v, size, f);
            }
            tiles_build_bones(sc->bones[z][v], v, size);
        }
    }
    return true;
}

void scene_free(Scene *sc)
{
    sprite_cache_free(&sc->sprites);
    anim_free(&sc->anim);
    tiles_cache_free(&sc->tiles);
    /* free(NULL) does nothing, so blocks never allocated are fine here. */
    free(sc->cell_ramps);
    free(sc->cell_pixels);
    free(sc->cell_cover);
    free(sc->cell_dirty);
    free(sc->keys);
    free(sc->keys_terrain);
    free(sc->trees);
    free(sc->items);
    free(sc->items_spare);
    free(sc->poses);
    sc->cell_ramps = NULL;
    sc->cell_pixels = NULL;
    sc->cell_cover = NULL;
    sc->cell_dirty = NULL;
    sc->cell_ramps_capacity = 0;
    sc->keys = NULL;
    sc->keys_terrain = NULL;
    sc->keys_capacity = 0;
    sc->keys_valid = false;
    sc->trees = NULL;
    sc->items = NULL;
    sc->items_spare = NULL;
    sc->items_capacity = 0;
    sc->poses = NULL;
    canvas_free(&sc->ground);
}

void scene_reset(Scene *sc)
{
    sprite_cache_reset(&sc->sprites);
    anim_reset(&sc->anim);
    tiles_cache_reset(&sc->tiles);
    sc->keys_valid = false;
    forget_cell_colours(sc); /* a new world's cell can match an old one's index */
    sc->ground_valid = false; /* a new world can have the same tick number */
}

void scene_update(Scene *sc, const Sim *s, int ticks_per_second)
{
    anim_update(&sc->anim, s, ticks_per_second);
}

int scene_ambient_frame(uint64_t water_ms)
{
    /* Cut to 30 bits so it fits an int; it only has to differ from the last
     * frame's, and wraps once in about three years of window time. */
    return (int)((water_ms / SCENE_AMBIENT_MS) & 0x3FFFFFFFu);
}

/* A plant's colour, turning to straw as it withers. */
static Colour plant_colour(const Sim *s, int i)
{
    const World *w = &s->world;
    Colour plant = palette_plant(&w->plant[i]);
    if (w->veg_wither[i] == 0) {
        return plant;
    }
    int shade = palette_wither_shade(w->veg_wither[i], s->set.veg_wither_ticks);
    return palette_mix(plant, PALETTE_WITHERED,
                       (float)shade / (float)PALETTE_WITHER_SHADES);
}

static Colour carrion_colour(const Sim *s, int i)
{
    float energy = s->world.carrion[i];
    float amount = energy / (energy + s->set.bite_meat_energy);
    return palette_mix(BONE, PALETTE_CARRION, amount);
}

/*
 * The colour a body must wear to match this cell's region, as a creature of
 * that hue is drawn: through render_display_hue, the same squeeze creatures go
 * through. Without it the tint would show the raw hue, and a matching creature
 * would look nothing like its ground. False for deep water and rock.
 */
static bool region_colour(const Sim *s, int i, Colour *out)
{
    int region = climate_region(&s->world, i, &s->set);
    if (region == REGION_NONE) {
        return false; /* -1: not an index, so climate_region_hue is not asked */
    }
    *out = palette_hue(render_display_hue(climate_region_hue(region)),
                       REGION_SATURATION);
    return true;
}

/* A cell's three ground colours, from its kind of ground alone: soil changes
 * with its fertility, water and snow. */
static GroundRamp base_ramp(const Sim *s, int i)
{
    const World *w = &s->world;
    switch (w->terrain[i]) {
    case TERRAIN_WATER: return RAMP_WATER;
    case TERRAIN_SHALLOW: return RAMP_SHALLOW;
    case TERRAIN_ROCK: return RAMP_ROCK;
    default: return ramp_soil(w->fertility[i], w->water[i], w->snow[i]);
    }
}

/* The colours a cell's tile is drawn in: its ground's, with a little of its
 * region's colour. */
static GroundRamp cell_ramp(const Sim *s, int i)
{
    GroundRamp g = base_ramp(s, i);
    Colour region;
    if (region_colour(s, i, &region)) {
        g = ramp_ground_mix(g, region, REGION_TINT);
    }
    return g;
}

/* A cell as one flat colour, for the furthest zoom: its ground's middle colour,
 * under its carrion or plant. */
static Colour flat_ground(const Sim *s, int i)
{
    const World *w = &s->world;
    if (w->terrain[i] != TERRAIN_SOIL) {
        return base_ramp(s, i).c[1];
    }
    /* Only the middle step: working out all three would waste two. */
    Colour ground = ramp_soil_step(w->fertility[i], w->water[i], w->snow[i], 1);
    if (w->carrion[i] > 0.0f) {
        return carrion_colour(s, i);
    }
    if (w->veg[i] > 0) {
        float t = (float)(w->veg[i] + 1) / (float)(VEG_MAX + 1);
        return palette_mix(ground, plant_colour(s, i), t);
    }
    return ground;
}

/* Too small for streaks: rain is a pale veil over the cell's colour. */
static Colour flat_colour(const Sim *s, int i)
{
    Colour ground = flat_ground(s, i);
    Colour region;
    if (region_colour(s, i, &region)) {
        ground = palette_mix(ground, region, REGION_TINT_FLAT);
    }
    return s->world.rain[i] > 0.0f ? palette_mix(ground, PALETTE_RAIN, 0.12f)
                                   : ground;
}

/* Steps of the sine table per radian. */
#define SINE_PER_RADIAN ((float)SCENE_SINE_STEPS / TWO_PI)
/* Added to a step number before it is cut to a whole number: a whole number of
 * turns, so the step it lands on is the same, and large enough that the number
 * is never negative. Converting a positive float to int drops its fraction,
 * which is then the same as rounding down, with no call to floorf. */
#define SINE_OFFSET (64.0f * (float)SCENE_SINE_STEPS)

/* The sine table at a step number (a float, any size above -SINE_OFFSET). */
static float sine_at(const Scene *sc, float step)
{
    /* & keeps the low bits: the step wrapped into the table. */
    return sc->sine[(int)(step + SINE_OFFSET) & (SCENE_SINE_STEPS - 1)];
}

/* A shade of 0..1 cut into the three steps of a ground colour. */
static int shade_step(float n)
{
    return n < 0.33f ? 0 : (n < 0.7f ? 1 : 2);
}

/*
 * The shade step of water at world pixel (ax, ay) whose wave is at wave
 * (0..1): shade_step of wave plus a little noise, (hash - 0.5) * 0.18, so
 * never more than 0.09 either way. The noise can only matter when the wave is
 * that close to a cut, so the hash is worked out only then (the margin is a
 * hair wider than 0.09, so rounding cannot change an answer).
 */
static int water_step(float wave, int ax, int ay)
{
    const float margin = 0.0901f;
    if (wave + margin < 0.33f) {
        return 0;
    }
    if (wave - margin >= 0.33f && wave + margin < 0.7f) {
        return 1;
    }
    if (wave - margin >= 0.7f) {
        return 2;
    }
    return shade_step(wave + (gfx_hash(ax, ay, 4) - 0.5f) * 0.18f);
}

/* A pixel's colour after its edge: banks are fixed colours, the rest darken or
 * lighten the ground under them. */
static Colour edge_colour(Colour c, unsigned edge)
{
    switch (edge) {
    case EDGE_BANK: return RAMP_BANK;
    case EDGE_BANK_DARK: return RAMP_BANK_DARK;
    case EDGE_SHADOW: return ramp_shade(c, -0.35f);
    case EDGE_ROCK_LIT: return ramp_shade(c, 0.25f);
    case EDGE_ROCK_SPECK: return ramp_shade(c, -0.4f);
    case EDGE_GRASS_SPECK: return ramp_shade(c, 0.25f);
    case EDGE_ROCK_LIT_SPECK: return ramp_shade(ramp_shade(c, 0.25f), -0.4f);
    default: return c;
    }
}

/* A cell's pixel at an edge and step, worked out the first time it is needed. */
static uint32_t edge_pixel(GroundPixels *gp, const GroundRamp *ramp,
                           unsigned edge, int step)
{
    if ((gp->done & (1u << edge)) == 0) {
        for (int k = 0; k < 3; k++) {
            gp->pixel[edge][k] = to_pixel(edge_colour(ramp->c[k], edge));
        }
        gp->done |= 1u << edge;
    }
    return gp->pixel[edge][step];
}

/* A scale by scale block, cut at the canvas's edges. Written straight into the
 * pixels: the ground is the most drawn thing there is. */
static void put_block(Canvas *c, int x, int y, int scale, uint32_t colour)
{
    for (int j = y; j < y + scale; j++) {
        if (j < 0 || j >= c->h) {
            continue;
        }
        for (int i = x; i < x + scale; i++) {
            if (i >= 0 && i < c->w) {
                c->px[j * c->w + i] = colour;
            }
        }
    }
}

/* Where one cell is drawn, and what its tile needs to know. */
typedef struct {
    int x, y;    /* the cell, inside the world */
    int sx, sy;  /* its top left corner on the canvas */
    int size, scale;
    int ring_at; /* the cell's place in cell_ramps and cell_pixels */
    int ring_w;  /* how far apart two rows of those are */
    float time;  /* the water clock, in seconds */
} GroundCell;

/* The nine cells a tile can show, found once per tile rather than per pixel.
 * Source k is the cell dx = k % 3 - 1, dy = k / 3 - 1 from the tile's own. */
typedef struct {
    GroundPixels *pixels[9];
    const GroundRamp *ramps[9];
} TileCells;

/* The colour of one of a tile's looks at one shade step. */
static uint32_t look_pixel(const TileCells *t, uint16_t look, int step)
{
    int source = (int)TILE_SOURCE(look);
    unsigned alt = TILE_ALT(look);
    if (alt != 0) {
        source = (int)alt - 1; /* the other water's colour */
    }
    unsigned edge = TILE_EDGE(look);
    return edge == EDGE_NONE ? t->pixels[source]->pixel[EDGE_NONE][step]
                             : edge_pixel(t->pixels[source], t->ramps[source], edge, step);
}

/* One cell's ground, from its autotile: first a colour for each of the tile's
 * looks, then each pixel picks its look's colour; water picks among three by
 * the shade of the water at that point. */
static void draw_ground_tile(Scene *sc, Canvas *c, const GroundCell *g,
                             const TileEntry *tile)
{
    TileCells cells;
    for (int k = 0; k < 9; k++) {
        int at = g->ring_at + (k / 3 - 1) * g->ring_w + (k % 3 - 1);
        cells.pixels[k] = &sc->cell_pixels[at];
        cells.ramps[k] = &sc->cell_ramps[at];
    }
    /* A water look fills three slots, so at most three per pixel. */
    uint32_t palette[3 * TILE_MAX * TILE_MAX];
    int slots = 0;
    for (int k = 0; k < tile->looks; k++) {
        uint16_t look = tile->look[k];
        if ((look & TILE_INK_WET) != 0) {
            for (int step = 0; step < 3; step++) {
                palette[slots++] = look_pixel(&cells, look, step);
            }
        } else {
            palette[slots++] = look_pixel(&cells, look, (int)TILE_SHADE(look));
        }
    }
    /* Copied into locals: the pixel writes below could, as far as the compiler
     * knows, change any int in *g or *c, so it would read them again after
     * every write. A local whose address is never taken cannot be changed so. */
    int size = g->size, scale = g->scale, sx = g->sx, stride = c->w;
    uint32_t *px = c->px;
    int unit = 16 / size; /* the water's pattern is laid out in 16ths of a cell */
    float wave_x = 0.07f * SINE_PER_RADIAN; /* steps per 16th of a cell across */
    int span = size * scale;
    /* The whole tile on the canvas: no pixel needs its own check. */
    bool inside = sx >= 0 && g->sy >= 0 && sx + span <= c->w && g->sy + span <= c->h;
    int ax0 = g->x * size * unit;
    uint32_t row_pixels[TILE_MAX];
    for (int ly = 0; ly < size; ly++) {
        /* The water's shade (v9:535), in pixels of a 16-pixel tile:
         * sin(ax 0.07 + ay 0.18 + sin(ay 0.05 + t 0.4) 2) / 2 + 1/2 + noise.
         * Everything but ax is the same along a row, so it is added up here. */
        int ay = (g->y * size + ly) * unit;
        float drift = sine_at(sc, ((float)ay * 0.05f + g->time * 0.4f) * SINE_PER_RADIAN);
        float row = ((float)ay * 0.18f + drift * 2.0f) * SINE_PER_RADIAN;
        const uint16_t *inks = tile->ink + ly * size;
        for (int lx = 0; lx < size; lx++) {
            unsigned ink = inks[lx];
            if ((ink & TILE_INK_WET) == 0) {
                row_pixels[lx] = palette[ink];
            } else {
                int ax = ax0 + lx * unit;
                float wave = sine_at(sc, (float)ax * wave_x + row) * 0.5f + 0.5f;
                row_pixels[lx] = palette[TILE_INK_SLOT(ink) +
                                         (unsigned)water_step(wave, ax, ay)];
            }
        }
        /* The row onto the canvas, each pixel scale by scale. */
        int py = g->sy + ly * scale;
        if (inside && scale == 1) {
            /* memcpy copies a block of bytes: the whole row at once. */
            memcpy(px + py * stride + sx, row_pixels, (size_t)size * sizeof(uint32_t));
        } else if (inside) {
            for (int j = 0; j < scale; j++) {
                uint32_t *out = px + (py + j) * stride + sx;
                for (int lx = 0; lx < size; lx++) {
                    for (int i = 0; i < scale; i++) {
                        out[lx * scale + i] = row_pixels[lx];
                    }
                }
            }
        } else {
            for (int lx = 0; lx < size; lx++) {
                put_block(c, sx + lx * scale, py, scale, row_pixels[lx]);
            }
        }
    }
}

/* The colour of a cell's carrion stain, darkened and half sunk into the ground
 * so the bones stand out on it, the redder the more meat is left. */
static Colour stain_colour(const Scene *sc, const Sim *s, int i, int ring_at)
{
    return palette_mix(sc->cell_ramps[ring_at].c[1],
                       ramp_shade(carrion_colour(s, i), -0.35f), 0.5f);
}

/* What a cell's carrion looks like, as one number: 0 for none, else its stain's
 * pixel (never 0, as a pixel's top byte is 0xFF). When it differs from what
 * was drawn, the cell is drawn again. */
static uint32_t cover_of(const Scene *sc, const Sim *s, int i, int ring_at)
{
    const World *w = &s->world;
    if (w->terrain[i] != TERRAIN_SOIL || w->carrion[i] <= 0.0f) {
        return 0;
    }
    return to_pixel(stain_colour(sc, s, i, ring_at));
}

/* Bones on a dried stain. */
static void draw_bones(Scene *sc, Canvas *c, const GroundCell *g, uint32_t stain)
{
    int z = g->size == 16 ? 0 : 1;
    int v = tiles_variant(g->x, g->y);
    Colour bone_dark = ramp_shade(BONE, -0.3f);
    const uint32_t colours[4] = {0, to_pixel(BONE), to_pixel(bone_dark), stain};
    canvas_picture(c, sc->bones[z][v], g->size, g->size, g->size, g->sx, g->sy,
                   g->scale, false, colours, whole(c));
}

/* Whether rain shows on a cell. Harder rain falls on more cells: variant 0
 * shows any rain, variant 3 only the heaviest. The variant is a hash of the
 * place, so this draws no random number from the simulation. */
static bool rain_shows(const World *w, int i, int variant)
{
    return w->rain[i] * (float)TILE_VARIANTS > (float)variant;
}

/* The blocks kept per cell of the view and its ring, grown to need cells.
 * False when memory runs out; what was there is kept, to free later. */
static bool grow_cell_blocks(Scene *sc, int need)
{
    if (need <= sc->cell_ramps_capacity) {
        return true;
    }
    /* Only a zoom or a resize shows more cells, so this runs only then.
     * realloc keeps the old block if it fails, so it is kept to free. */
    GroundRamp *ramps = realloc(sc->cell_ramps, (size_t)need * sizeof(GroundRamp));
    if (ramps == NULL) {
        return false;
    }
    sc->cell_ramps = ramps;
    GroundPixels *pixels = realloc(sc->cell_pixels, (size_t)need * sizeof(GroundPixels));
    if (pixels == NULL) {
        return false; /* capacity unchanged: the next frame tries again */
    }
    sc->cell_pixels = pixels;
    uint32_t *cover = realloc(sc->cell_cover, (size_t)need * sizeof(uint32_t));
    if (cover == NULL) {
        return false;
    }
    sc->cell_cover = cover;
    bool *dirty = realloc(sc->cell_dirty, (size_t)need * sizeof(bool));
    if (dirty == NULL) {
        return false;
    }
    sc->cell_dirty = dirty;
    sc->cell_ramps_capacity = need;
    forget_cell_colours(sc);
    return true;
}

/* Marks a cell of the ring to be drawn again, and the cells around it: their
 * tiles' ragged edges show its colours too. */
static void mark_with_neighbours(Scene *sc, int at, int ring_w, int ring_h)
{
    int r = at / ring_w, k = at % ring_w;
    for (int dr = -1; dr <= 1; dr++) {
        for (int dk = -1; dk <= 1; dk++) {
            int rr = r + dr, kk = k + dk;
            if (rr >= 0 && rr < ring_h && kk >= 0 && kk < ring_w) {
                sc->cell_dirty[rr * ring_w + kk] = true;
            }
        }
    }
}

/* Each visible cell's colours and the ring around them, into sc->cell_ramps
 * and sc->cell_pixels; a cell whose colours changed is marked dirty, with its
 * neighbours. False when the blocks could not grow: the caller then draws
 * flat colours. */
static bool fill_cell_ramps(Scene *sc, const Sim *s, const CameraCells *v)
{
    int ring_w = v->cols + 2, ring_h = v->rows + 2;
    if (!grow_cell_blocks(sc, ring_w * ring_h)) {
        return false;
    }
    const World *w = &s->world;
    for (int r = 0; r < ring_h; r++) {
        for (int k = 0; k < ring_w; k++) {
            /* world_idx wraps: the ring's first row and column lie one cell
             * before the first visible one, which may be the world's far side. */
            int i = world_idx(w, v->first_x + k - 1, v->first_y + r - 1);
            int at = r * ring_w + k;
            GroundPixels *gp = &sc->cell_pixels[at];
            int region = climate_region(w, i, &s->set);
            /* Unchanged since the last redraw: its colours still hold. */
            if (gp->known && gp->cell == i && gp->terrain == w->terrain[i] &&
                gp->region == region && gp->fertility == w->fertility[i] &&
                gp->water == w->water[i] && gp->snow == w->snow[i]) {
                continue;
            }
            GroundRamp ramp = cell_ramp(s, i);
            /* The soil's fertility and water drift a little every tick, which
             * mostly leaves its colours the same to the byte: then nothing
             * needs drawing again, and the edges' colours still hold too. */
            bool same = gp->known && gp->cell == i &&
                        memcmp(&ramp, &sc->cell_ramps[at], sizeof(ramp)) == 0;
            gp->known = true;
            gp->cell = i;
            gp->terrain = w->terrain[i];
            gp->region = region;
            gp->fertility = w->fertility[i];
            gp->water = w->water[i];
            gp->snow = w->snow[i];
            if (same) {
                continue;
            }
            sc->cell_ramps[at] = ramp;
            for (int step = 0; step < 3; step++) {
                gp->pixel[EDGE_NONE][step] = to_pixel(ramp.c[step]);
            }
            gp->done = 1u << EDGE_NONE; /* the other edges when first needed */
            mark_with_neighbours(sc, at, ring_w, ring_h);
        }
    }
    return true;
}

/* Every cell's tile key, worked out again only when the terrain has changed
 * (in the program, only for a new world). False when memory runs out. */
static bool refresh_keys(Scene *sc, const World *w)
{
    int n = w->w * w->h;
    if (n > sc->keys_capacity) {
        /* Only the first frame, or a larger new world. */
        uint32_t *keys = realloc(sc->keys, (size_t)n * sizeof(uint32_t));
        if (keys == NULL) {
            return false;
        }
        sc->keys = keys;
        uint8_t *terrain = realloc(sc->keys_terrain, (size_t)n);
        if (terrain == NULL) {
            return false;
        }
        sc->keys_terrain = terrain;
        sc->keys_capacity = n;
        sc->keys_valid = false;
    }
    /* memcmp compares two blocks byte by byte: 0 when they are the same. */
    if (sc->keys_valid && memcmp(sc->keys_terrain, w->terrain, (size_t)n) == 0) {
        return true;
    }
    for (int y = 0; y < w->h; y++) {
        for (int x = 0; x < w->w; x++) {
            sc->keys[y * w->w + x] = tiles_key(w, x, y);
        }
    }
    memcpy(sc->keys_terrain, w->terrain, (size_t)n);
    sc->keys_valid = true;
    return true;
}

/* Every cell the camera sees, as flat colours: the furthest zoom. */
static void draw_flat_cells(Canvas *c, const Camera *cam, const Sim *s,
                            const CameraCells *v, int view_x, int view_y)
{
    const World *w = &s->world;
    int ppc = camera_ppc(cam);
    for (int r = 0; r < v->rows; r++) {
        int y = (v->first_y + r) % w->h;
        for (int k = 0; k < v->cols; k++) {
            int x = (v->first_x + k) % w->w;
            canvas_rect(c, view_x + v->screen_x + k * ppc,
                        view_y + v->screen_y + r * ppc, ppc, ppc,
                        to_pixel(flat_colour(s, y * w->w + x)));
        }
    }
}

/* How much of the ground layer a redraw draws again. */
typedef enum {
    REDRAW_ALL,   /* every cell, its colours worked out where they changed */
    REDRAW_CELLS  /* only the dirty cells, and those with water if it drifted */
} Redraw;

/*
 * Every cell the camera sees. With REDRAW_CELLS the view is the one last drawn
 * whole, and only the cells marked dirty are drawn again, and also the cells
 * with water on them when drifted (the water's slow ripple moved on). True
 * when the cells were drawn as tiles, so a later call may redraw only some.
 */
static bool draw_terrain(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                         uint64_t water_ms, int view_x, int view_y, Redraw redraw,
                         bool drifted)
{
    const World *w = &s->world;
    CameraCells v;
    camera_cells(cam, &v);
    int size = camera_sprite_size(cam);
    if (size == 0 || !refresh_keys(sc, w) ||
        (redraw == REDRAW_ALL && !fill_cell_ramps(sc, s, &v))) {
        draw_flat_cells(c, cam, s, &v, view_x, view_y);
        return false;
    }
    if (sc->tiles_level != cam->level) {
        tiles_cache_reset(&sc->tiles);
        sc->tiles_level = cam->level;
    }
    int ppc = camera_ppc(cam);
    GroundCell g = {
        .size = size,
        .scale = camera_sprite_scale(cam),
        .ring_w = v.cols + 2,
        /* The time of the water's drift step, not of the frame: the layer is
         * redrawn once a step, so a redraw for a tick shows the same water. */
        .time = (float)(water_ms / SCENE_DRIFT_MS * SCENE_DRIFT_MS) / 1000.0f,
    };
    for (int r = 0; r < v.rows; r++) {
        g.y = (v.first_y + r) % w->h;
        g.sy = view_y + v.screen_y + r * ppc;
        for (int k = 0; k < v.cols; k++) {
            g.x = (v.first_x + k) % w->w;
            g.sx = view_x + v.screen_x + k * ppc;
            g.ring_at = (r + 1) * g.ring_w + (k + 1);
            int i = g.y * w->w + g.x;
            const TileEntry *tile = tiles_cache_get(&sc->tiles, sc->keys[i], size);
            if (redraw == REDRAW_CELLS && !sc->cell_dirty[g.ring_at] &&
                !(drifted && tile != NULL && tile->has_wet)) {
                continue; /* what was drawn is still right */
            }
            if (tile != NULL) {
                draw_ground_tile(sc, c, &g, tile);
            }
            uint32_t cover = cover_of(sc, s, i, g.ring_at);
            if (cover != 0) {
                draw_bones(sc, c, &g, cover);
            }
            sc->cell_cover[g.ring_at] = cover;
        }
    }
    return true;
}

static Colour creature_colour(const Creature *c)
{
    return palette_creature(c->genome.g[GENE_HUE], render_meat_share(c->level));
}

/* How many of its remembered hits a creature took in the last
 * SCENE_SCAR_TICKS ticks. Only the first hits_taken entries of the ring mean
 * anything (see Creature). */
static int recent_hit_count(const Creature *cr, uint64_t tick)
{
    uint32_t kept = cr->hits_taken < HITS_KEPT ? cr->hits_taken : HITS_KEPT;
    int n = 0;
    for (uint32_t k = 0; k < kept; k++) {
        /* Written as an addition: tick - when, on unsigned numbers, would wrap
         * round to a huge number if a hit were ever stamped after tick. */
        uint64_t when = cr->recent_hits[k].tick;
        if (when + SCENE_SCAR_TICKS >= tick) {
            n++;
        }
    }
    return n;
}

/* The scars over a body already drawn: body is its rows, size by size. */
static void draw_scars(Canvas *c, const uint64_t *body, int size, int x, int y,
                       int scale, bool flip, int count, CanvasClip clip)
{
    if (count <= 0) {
        return;
    }
    uint64_t scars[SPRITE_MAX_COLS];
    sprite_scars(scars, body, size, count);
    canvas_mask_wide(c, scars, size, x, y, scale, flip, to_pixel(SCAR), clip);
}

/* One creature as a symbol, its tile cleared first so its eye shows black
 * and no plant shows through. A newborn is drawn at half size at first. */
static void draw_body(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                      const Creature *cr, int slot, const AnimPose *p, int sx,
                      int sy)
{
    int ppc = camera_ppc(cam);
    int size = camera_sprite_size(cam);
    int scale = camera_sprite_scale(cam);
    uint32_t colour = to_pixel(creature_colour(cr));
    canvas_rect(c, sx, sy, ppc, ppc, to_pixel(BACKGROUND));
    bool newborn = p->pop < 0.5f;
    if (newborn && size == 8) {
        canvas_rect(c, sx + ppc / 4, sy + ppc / 4, ppc / 2, ppc / 2, colour);
        return;
    }
    int drawn_size = newborn ? 8 : size;
    int inset = newborn ? ppc / 4 : 0;
    const SpriteMask *m = sprite_get(&sc->sprites, slot, cr->id, &cr->genome,
                                     drawn_size, p->frame);
    if (m == NULL) {
        return;
    }
    canvas_mask(c, m->body, drawn_size, sx + inset, sy + inset, scale, p->flip,
                colour);
    canvas_mask(c, m->white, drawn_size, sx + inset, sy + inset, scale, p->flip,
                to_pixel(WHITE));
    /* The scars want 64-bit rows; a uint16_t widens to uint64_t with no loss. */
    uint64_t rows[16];
    for (int j = 0; j < 16; j++) {
        rows[j] = m->body[j];
    }
    draw_scars(c, rows, drawn_size, sx + inset, sy + inset, scale, p->flip,
               recent_hit_count(cr, s->tick), whole(c));
}

/* Where on the canvas the world shows, and whether it shows whole along each
 * axis. A whole axis is not repeated on screen, so a body hanging over its
 * edge must also be drawn on the other side, as the world wraps. */
typedef struct {
    CanvasClip clip;
    bool whole_x, whole_y;
    int world_px_w, world_px_h; /* the whole world's size in screen pixels */
} WorldArea;

static WorldArea world_area(const Camera *cam, int view_x, int view_y)
{
    CameraCells v;
    camera_cells(cam, &v);
    int ppc = camera_ppc(cam);
    int left = v.screen_x > 0 ? v.screen_x : 0;
    int top = v.screen_y > 0 ? v.screen_y : 0;
    int right = v.screen_x + v.cols * ppc;
    int bottom = v.screen_y + v.rows * ppc;
    right = right < cam->view_w ? right : cam->view_w;
    bottom = bottom < cam->view_h ? bottom : cam->view_h;
    WorldArea a;
    a.clip = (CanvasClip){view_x + left, view_y + top, right - left, bottom - top};
    a.world_px_w = cam->world_w * ppc;
    a.world_px_h = cam->world_h * ppc;
    a.whole_x = a.world_px_w <= cam->view_w;
    a.whole_y = a.world_px_h <= cam->view_h;
    return a;
}

/* The top left corner of a body drawn tiles wide whose cell's corner is at
 * (sx, sy): its feet on the bottom of its own cell, centred across it. */
static void large_corner(int sx, int sy, int ppc, int tiles, int *x, int *y)
{
    int extent = tiles * ppc;
    *x = sx + ppc / 2 - extent / 2;
    *y = sy + ppc - extent;
}

/* One picture of a large body with its corner at (x, y), unless it misses the
 * world's area entirely. */
static void draw_large_at(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                          const Creature *cr, const AnimPose *p, int tiles,
                          int x, int y, CanvasClip clip)
{
    int extent = tiles * camera_ppc(cam);
    if (x >= clip.x + clip.w || y >= clip.y + clip.h || x + extent <= clip.x ||
        y + extent <= clip.y) {
        return;
    }
    uint32_t colour = to_pixel(creature_colour(cr));
    int size = camera_sprite_size(cam);
    if (size == 0) {
        canvas_rect_clip(c, x, y, extent, extent, colour, clip);
        return;
    }
    const WideMask *m =
        sprite_get_wide(&sc->sprites, cr->id, &cr->genome, tiles, size, p->frame);
    if (m == NULL) {
        return;
    }
    int scale = camera_sprite_scale(cam);
    canvas_mask_wide(c, m->outline, m->size, x, y, scale, p->flip,
                     to_pixel(BACKGROUND), clip);
    canvas_mask_wide(c, m->body, m->size, x, y, scale, p->flip, colour, clip);
    canvas_mask_wide(c, m->white, m->size, x, y, scale, p->flip, to_pixel(WHITE),
                     clip);
    draw_scars(c, m->body, m->size, x, y, scale, p->flip,
               recent_hit_count(cr, s->tick), clip);
}

/* A body drawn several tiles wide, over the ground and over smaller bodies,
 * with no cell cleared: its outline keeps it apart from what is behind. Only
 * drawing: in the simulation it still stands on one cell. */
static void draw_large(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                       const Creature *cr, const AnimPose *p, int tiles, int sx,
                       int sy, const WorldArea *area)
{
    int x, y;
    large_corner(sx, sy, camera_ppc(cam), tiles, &x, &y);
    /* Along a whole axis, the copies one world to either side too: at most
     * one of them lands in view, the one across the edge. */
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if ((dx != 0 && !area->whole_x) || (dy != 0 && !area->whole_y)) {
                continue;
            }
            draw_large_at(sc, c, cam, s, cr, p, tiles, x + dx * area->world_px_w,
                          y + dy * area->world_px_h, area->clip);
        }
    }
}

/* How many tiles wide this creature is drawn. A newborn is drawn small,
 * whatever its size, while it pops in. */
static int drawn_tiles(const Creature *cr, const Sim *s, const AnimPose *p)
{
    if (p != NULL && p->pop < 0.5f) {
        return 1;
    }
    return sprite_tiles_for(creature_size(cr, &s->set));
}

/* The mark over the cell a creature is biting: a red flash for a fight, and
 * crumbs whose colour says what is being eaten. */
static void draw_mark(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                      const Creature *cr, const AnimPose *p, uint64_t body_ms,
                      int view_x, int view_y)
{
    int ppc = camera_ppc(cam);
    int size = camera_sprite_size(cam);
    int scale = camera_sprite_scale(cam);
    int z = size == 16 ? 0 : 1;
    int sx, sy;
    camera_to_screen(cam, (float)p->target_x, (float)p->target_y, &sx, &sy);
    if (sx <= -ppc || sy <= -ppc || sx >= cam->view_w || sy >= cam->view_h) {
        return;
    }
    /* The id keeps neighbouring marks out of step. */
    int f = (int)((body_ms / MARK_FRAME_MS + cr->id) % TILE_MARK_FRAMES);
    const uint16_t *rows = sc->crumbs[z][f];
    Colour colour = MEAT_CRUMB;
    if (p->bite == POSE_ATTACK) {
        rows = sc->slash[z][f];
        colour = SLASH;
    } else if (p->bite == BITE_PLANT) {
        const World *w = &s->world;
        Colour plant = palette_plant(&w->plant[p->target_y * w->w + p->target_x]);
        colour = palette_mix(plant, WHITE, 0.6f);
    }
    canvas_mask(c, rows, size, view_x + sx, view_y + sy, scale, false,
                to_pixel(colour));
}

/* After every body, so no creature's cleared tile can hide a mark. */
static void draw_marks(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                       float tick_fraction, uint64_t body_ms, int view_x,
                       int view_y)
{
    if (camera_sprite_size(cam) == 0) {
        return; /* a coloured square per cell has no room for a mark */
    }
    int n = s->pool.capacity < sc->anim.capacity ? s->pool.capacity
                                                 : sc->anim.capacity;
    for (int i = 0; i < n; i++) {
        const Creature *cr = &s->pool.items[i];
        /* Tested first: most creatures are not biting, and this saves
         * working out their pose a second time. */
        if (!cr->alive || !sc->anim.slots[i].seen ||
            (cr->current.kind != ACT_BITE && cr->current.kind != ACT_ATTACK)) {
            continue;
        }
        AnimPose p;
        anim_pose(&sc->anim, s, i, tick_fraction, body_ms, &p);
        if (p.bite != BITE_NOTHING) {
            draw_mark(sc, c, cam, s, cr, &p, body_ms, view_x, view_y);
        }
    }
}

/* True when the ground layer was drawn for this view: same size, zoom and
 * place. What else may have changed is for place_ground to find out. */
static bool ground_same_view(const Scene *sc, const Camera *cam)
{
    return sc->ground_valid && sc->ground.w == cam->view_w &&
           sc->ground.h == cam->view_h && sc->ground_level == cam->level &&
           sc->ground_cx == cam->cx && sc->ground_cy == cam->cy;
}

/* Whether the terrain differs from the one the keys were worked out from. */
static bool terrain_changed(const Scene *sc, const World *w)
{
    int n = w->w * w->h;
    return !sc->keys_valid || n > sc->keys_capacity ||
           memcmp(sc->keys_terrain, w->terrain, (size_t)n) != 0;
}

/*
 * Which cells of the view must be drawn again: after a tick, those whose
 * carrion changed; when colours is true (with or without a tick), those whose
 * ground colours changed, with their neighbours. Clears the old marks first.
 */
static void mark_changed_cells(Scene *sc, const Camera *cam, const Sim *s,
                               bool ticked, bool colours)
{
    CameraCells v;
    camera_cells(cam, &v);
    int ring_w = v.cols + 2;
    int cells = ring_w * (v.rows + 2);
    for (int at = 0; at < cells; at++) {
        sc->cell_dirty[at] = false;
    }
    if (colours) {
        /* The blocks are already the view's size: this cannot fail here. */
        fill_cell_ramps(sc, s, &v);
    }
    if (!ticked) {
        return;
    }
    const World *w = &s->world;
    for (int r = 0; r < v.rows; r++) {
        int y = (v.first_y + r) % w->h;
        for (int k = 0; k < v.cols; k++) {
            int x = (v.first_x + k) % w->w;
            int at = (r + 1) * ring_w + (k + 1);
            if (cover_of(sc, s, y * w->w + x, at) != sc->cell_cover[at]) {
                sc->cell_dirty[at] = true;
            }
        }
    }
}

/* Draws the view's ground, black around a world smaller than the view, at
 * (x, y). Both callers below need exactly this. True when drawn as tiles. */
static bool draw_ground(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                        uint64_t water_ms, int x, int y)
{
    /* True black around a world smaller than the view. Where the world is,
     * every cell's ground covers it, so nothing needs filling first. */
    CameraCells v;
    camera_cells(cam, &v);
    int ppc = camera_ppc(cam);
    bool covered = v.screen_x <= 0 && v.screen_y <= 0 &&
                   v.screen_x + v.cols * ppc >= cam->view_w &&
                   v.screen_y + v.rows * ppc >= cam->view_h;
    if (!covered) {
        canvas_rect(c, x, y, cam->view_w, cam->view_h, canvas_rgb(0, 0, 0));
    }
    return draw_terrain(sc, c, cam, s, water_ms, x, y, REDRAW_ALL, false);
}

/*
 * The ground into the frame, redrawn only where something in it has changed.
 * A new view draws it all. On the same view: a tick redraws the cells whose
 * carrion changed, and at most once a SCENE_COLOUR_MS those whose soil colour
 * changed (a change that slow is never seen); a new terrain draws it all; a
 * drift step redraws the cells with water. The World zoom's flat colours are
 * cheap, and redrawn whole on each tick, as before.
 */
static void place_ground(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                         uint64_t water_ms, int view_x, int view_y)
{
    uint64_t drift = water_ms / SCENE_DRIFT_MS;
    bool ticked = sc->ground_tick != s->tick;
    bool drifted = sc->ground_drift_step != drift;
    /* Unsigned: water_ms below the last refresh (a test going back in time)
     * is a new clock, and refreshes too. */
    bool due = water_ms < sc->ground_colour_ms ||
               water_ms - sc->ground_colour_ms >= SCENE_COLOUR_MS;
    if (ground_same_view(sc, cam) && sc->ground_tiled) {
        /* A tick inside the second leaves its colour change for later; it is
         * remembered, so it still shows if no tick follows (paused, or the
         * world over). */
        if (ticked && !due) {
            sc->ground_colours_pending = true;
        }
        bool colours = (ticked || sc->ground_colours_pending) && due;
        if (!ticked && !drifted && !colours) {
            canvas_copy(c, &sc->ground, view_x, view_y);
            return;
        }
        if (!(ticked && terrain_changed(sc, &s->world))) {
            mark_changed_cells(sc, cam, s, ticked, colours);
            if (colours) {
                sc->ground_colour_ms = water_ms;
                sc->ground_colours_pending = false;
            }
            draw_terrain(sc, &sc->ground, cam, s, water_ms, 0, 0, REDRAW_CELLS, drifted);
            sc->ground_tick = s->tick;
            sc->ground_drift_step = drift;
            canvas_copy(c, &sc->ground, view_x, view_y);
            return;
        }
    } else if (ground_same_view(sc, cam) && !ticked) {
        /* Flat colours (the World zoom): no water moves, so only a tick. */
        canvas_copy(c, &sc->ground, view_x, view_y);
        return;
    }
    /* Out of memory for the layer: draw straight into the frame instead. */
    bool same_size = sc->ground.w == cam->view_w && sc->ground.h == cam->view_h;
    if (!same_size && !canvas_resize(&sc->ground, cam->view_w, cam->view_h)) {
        sc->ground_valid = false;
        draw_ground(sc, c, cam, s, water_ms, view_x, view_y);
        return;
    }
    sc->ground_tiled = draw_ground(sc, &sc->ground, cam, s, water_ms, 0, 0);
    sc->ground_valid = true;
    sc->ground_tick = s->tick;
    sc->ground_drift_step = drift;
    sc->ground_colour_ms = water_ms;
    sc->ground_colours_pending = false;
    sc->ground_level = cam->level;
    sc->ground_cx = cam->cx;
    sc->ground_cy = cam->cy;
    canvas_copy(c, &sc->ground, view_x, view_y);
}

/* ---- What moves over the ground every frame: the overlays ---- */

/* The overlays' clock in seconds, at the start of its step. It goes round once
 * an hour, so the float keeps whole steps exact for the sines: an hour of
 * steps is 36,000, far inside a float's 24 bits. */
static float ambient_seconds(uint64_t water_ms)
{
    return (float)((water_ms / SCENE_AMBIENT_MS) % 36000u) * 0.1f;
}

/* sin of an angle in radians, from the table. */
static float sine_of(const Scene *sc, float radians)
{
    return sine_at(sc, radians * SINE_PER_RADIAN);
}

/* Rounds to the nearest whole number, halves away from zero, as v9's
 * Math.round does for these small numbers within a pixel. */
static int round_to_int(float f)
{
    return f >= 0.0f ? (int)(f + 0.5f) : -(int)(-f + 0.5f);
}

/* What the overlays need about the frame. */
typedef struct {
    CanvasClip clip; /* where the world shows on the canvas */
    float t;         /* the overlays' clock, in seconds */
    int size, scale;
    int rain_frame;
    uint32_t glint, glint_shadow, foam, foam_shadow;
} Overlay;

/* One pixel of an overlay at tile pixel (lx, ly) of the cell drawn at (sx, sy).
 * Most fall inside the clip at scale 1: those are written straight into the
 * pixels, as there are thousands a frame; the rest go through the clipped
 * drawing. o->clip lies inside the canvas, so inside it is inside both. */
static void overlay_pixel(Canvas *c, const Overlay *o, int sx, int sy, int lx, int ly,
                          uint32_t colour)
{
    int x = sx + lx * o->scale, y = sy + ly * o->scale;
    if (o->scale == 1 && x >= o->clip.x && y >= o->clip.y &&
        x < o->clip.x + o->clip.w && y < o->clip.y + o->clip.h) {
        c->px[y * c->w + x] = colour;
        return;
    }
    canvas_pixel(c, x, y, o->scale, colour, o->clip);
}

/*
 * Glints (v9:540): on even rows of open water, runs of 5 pixels drifting 6
 * pixels a second, a run lit where its hash is above 0.93 and its own slow
 * wave is up. Coordinates are the world's, in pixels of the tile size.
 */
static void draw_glints(const Scene *sc, Canvas *c, const Overlay *o, const TileEntry *tile,
                        int x, int y, int sx, int sy)
{
    int size = o->size;
    float drift = o->t * 6.0f;
    int ax0 = x * size;
    for (int ly = 0; ly < size; ly++) {
        int ay = y * size + ly;
        if ((ay & 1) != 0) {
            continue;
        }
        int gy = ay >> 1; /* >> 1 halves, as v9's does */
        int first = (int)floorf(((float)ax0 + drift) / 5.0f);
        int last = (int)floorf(((float)(ax0 + size - 1) + drift) / 5.0f);
        for (int gx = first; gx <= last; gx++) {
            if (gfx_hash(gx, gy, 5) <= 0.93f ||
                sine_of(sc, o->t * 2.0f + gfx_hash(gx, gy, 6) * 6.0f) <= 0.2f) {
                continue;
            }
            /* The run's pixels: floor((ax + drift) / 5) == gx. */
            int from = (int)ceilf((float)(5 * gx) - drift) - ax0;
            int to = (int)ceilf((float)(5 * gx + 5) - drift) - ax0; /* one past */
            from = from < 0 ? 0 : from;
            to = to > size ? size : to;
            for (int lx = from; lx < to; lx++) {
                int at = ly * size + lx;
                if ((tile->ink[at] & TILE_INK_WET) == 0) {
                    continue;
                }
                unsigned edge = TILE_EDGE(tile->code[at]);
                if (edge == EDGE_NONE || edge == EDGE_SHADOW) {
                    overlay_pixel(c, o, sx, sy, lx, ly,
                                  edge == EDGE_NONE ? o->glint : o->glint_shadow);
                }
            }
        }
    }
}

/* Foam on water touching land, coming and going in a slow wave (v9:543). */
static void draw_foam(const Scene *sc, Canvas *c, const Overlay *o, const TileEntry *tile,
                      int x, int y, int sx, int sy)
{
    int size = o->size;
    for (int k = 0; k < tile->foams; k++) {
        int at = tile->foam[k];
        int lx = at % size, ly = at / size;
        int ax = x * size + lx, ay = y * size + ly;
        if (sine_of(sc, o->t * 2.5f + (float)(ax + ay) * 0.35f) > 0.1f) {
            bool shadow = TILE_EDGE(tile->code[at]) == EDGE_SHADOW;
            overlay_pixel(c, o, sx, sy, lx, ly, shadow ? o->foam_shadow : o->foam);
        }
    }
}

/* Whether a tile's pixel shows soil. */
static bool pixel_is_soil(const TileEntry *tile, int size, int lx, int ly)
{
    return tiles_key_class(tile->key, (int)TILE_SOURCE(tile->code[ly * size + lx])) ==
           CLASS_SOIL;
}

/*
 * Grass tufts (v9:553): round(fertility * 3) per soil cell, placed by hash, each
 * five pixels of light grass swaying with a slow wave. Only where the tile shows
 * soil at the tuft and two pixels below it. The count is the plan's call. The
 * row is my choice: 2 + hash * (size - 4), where v9 has 2 + hash * 14, so at
 * the 16-pixel size the tuft's lowest row is 13, not 15, and the pixel two
 * below it stays in the same tile; reading the tile below would need its key
 * looked up for every tuft. The 8-pixel size scales the same way.
 */
static void draw_tufts(Scene *sc, Canvas *c, const Overlay *o, const Sim *s,
                       const TileEntry *tile, int x, int y, int sx, int sy, int ring_at)
{
    const World *w = &s->world;
    int i = y * w->w + x;
    int count = round_to_int(w->fertility[i] * 3.0f);
    if (count <= 0) {
        return;
    }
    count = count > 3 ? 3 : count;
    int size = o->size;
    uint32_t light = edge_pixel(&sc->cell_pixels[ring_at], &sc->cell_ramps[ring_at],
                                EDGE_GRASS_SPECK, 2);
    for (int n = 0; n < count; n++) {
        int lx = 1 + (int)(gfx_hash(x, y, 20 + n) * (float)(size - 2));
        int ly = 2 + (int)(gfx_hash(x, y, 30 + n) * (float)(size - 4));
        if (!pixel_is_soil(tile, size, lx, ly) || !pixel_is_soil(tile, size, lx, ly + 2)) {
            continue;
        }
        int ax = x * size + lx;
        int sway = round_to_int(sine_of(sc, o->t * 1.6f + (float)ax * 0.15f) * 0.8f);
        overlay_pixel(c, o, sx, sy, lx - 1, ly, light);
        overlay_pixel(c, o, sx, sy, lx + 1, ly, light);
        overlay_pixel(c, o, sx, sy, lx + sway, ly - 1, light);
        overlay_pixel(c, o, sx, sy, lx - 1 + sway, ly - 2, light);
        overlay_pixel(c, o, sx, sy, lx + 1 + sway, ly - 2, light);
    }
}

/* Rain streaks over a cell, kept inside the world's area. */
static void draw_rain(Scene *sc, Canvas *c, const Overlay *o, int x, int y, int sx,
                      int sy)
{
    int z = o->size == 16 ? 0 : 1;
    const uint16_t *rows = sc->rain[z][tiles_variant(x, y)][o->rain_frame];
    uint32_t colour = to_pixel(PALETTE_RAIN);
    for (int j = 0; j < o->size; j++) {
        /* A streak is a few bits a row: skip the empty rows at once. */
        for (unsigned bits = rows[j]; bits != 0; bits &= bits - 1) {
            /* bits & (bits - 1) clears the lowest set bit; __builtin_ctz
             * would find it faster, but is not plain C, so count instead. */
            int bit = 0;
            while (((bits >> bit) & 1u) == 0) {
                bit++;
            }
            /* Column i of a row is bit (size - 1 - i), as canvas_mask reads it. */
            overlay_pixel(c, o, sx, sy, o->size - 1 - bit, j, colour);
        }
    }
}

static WorldArea world_area(const Camera *cam, int view_x, int view_y);

/* Everything that moves over the ground layer, cell by visible cell: rain, water
 * glints, foam and grass tufts. Not at the World zoom, whose flat colours have
 * no room for them (its rain is a veil in the colour). */
static void draw_overlays(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                          uint64_t water_ms, int view_x, int view_y)
{
    int size = camera_sprite_size(cam);
    /* The colours in cell_pixels belong to the layer as last drawn whole. */
    if (size == 0 || !sc->ground_valid || !sc->ground_tiled) {
        return;
    }
    Overlay o = {
        .clip = world_area(cam, view_x, view_y).clip,
        .t = ambient_seconds(water_ms),
        .size = size,
        .scale = camera_sprite_scale(cam),
        .rain_frame = scene_ambient_frame(water_ms) % TILE_RAIN_FRAMES,
        .glint = to_pixel(RAMP_GLINT),
        .glint_shadow = to_pixel(ramp_shade(RAMP_GLINT, -0.35f)),
        .foam = to_pixel(RAMP_FOAM),
        .foam_shadow = to_pixel(ramp_shade(RAMP_FOAM, -0.35f)),
    };
    const World *w = &s->world;
    CameraCells v;
    camera_cells(cam, &v);
    int ppc = camera_ppc(cam);
    int ring_w = v.cols + 2;
    for (int r = 0; r < v.rows; r++) {
        int y = (v.first_y + r) % w->h;
        int sy = view_y + v.screen_y + r * ppc;
        for (int k = 0; k < v.cols; k++) {
            int x = (v.first_x + k) % w->w;
            int sx = view_x + v.screen_x + k * ppc;
            int i = y * w->w + x;
            const TileEntry *tile = tiles_cache_get(&sc->tiles, sc->keys[i], size);
            if (tile == NULL) {
                continue;
            }
            if (tile->has_wet) {
                draw_glints(sc, c, &o, tile, x, y, sx, sy);
                draw_foam(sc, c, &o, tile, x, y, sx, sy);
            }
            if (w->terrain[i] == TERRAIN_SOIL) {
                draw_tufts(sc, c, &o, s, tile, x, y, sx, sy, (r + 1) * ring_w + (k + 1));
            }
            if (rain_shows(w, i, tiles_variant(x, y))) {
                draw_rain(sc, c, &o, x, y, sx, sy);
            }
        }
    }
}

/* ---- Trees and bodies, back to front ---- */

/* Where copies of something drawn at one place also show: along an axis where
 * the whole world fits, one world to either side, as the world wraps. Fills
 * dx and dy (in screen pixels) and returns how many, the place itself first. */
static int copy_offsets(const WorldArea *a, int dx[9], int dy[9])
{
    int n = 0;
    dx[n] = 0;
    dy[n] = 0;
    n++;
    for (int oy = -1; oy <= 1; oy++) {
        for (int ox = -1; ox <= 1; ox++) {
            if ((ox == 0 && oy == 0) || (ox != 0 && !a->whole_x) ||
                (oy != 0 && !a->whole_y)) {
                continue;
            }
            dx[n] = ox * a->world_px_w;
            dy[n] = oy * a->world_px_h;
            n++;
        }
    }
    return n;
}

/* The trunk's colours, fixed (v9:565). */
static const Colour TRUNK_LIGHT = {96, 62, 34};
static const Colour TRUNK_DARK = {70, 44, 24};
/* How much light a tree's shadow keeps, out of 256: v9's 0.55. */
#define TREE_SHADOW_KEEP 141

/* What one tree is drawn with this frame. */
typedef struct {
    const TreePicture *picture;
    TreeBox box;
    int x, y; /* the picture's top left corner on the canvas */
    int foot_x, foot_y; /* the foot pixel's corner on the canvas */
} TreeDraw;

/* The tree of the plant on cell i, whose cell's corner is at (sx, sy). */
static TreeDraw tree_draw(const Scene *sc, const Sim *s, int i, int sx, int sy,
                          int size, int scale, int season, float t)
{
    const World *w = &s->world;
    const Plant *p = &w->plant[i];
    int x = i % w->w, y = i / w->w;
    int density = w->veg[i] > VEG_MAX ? VEG_MAX : w->veg[i];
    bool big = plant_gives_shade(p, &s->set);
    bool bare = plant_is_bare(p, season);
    /* The sway (v9:566): round(sin(t * 1.3 + x) * 0.9), x the trunk's pixel. */
    int foot_px = x * size + size / 2 - 1;
    int sway = round_to_int(sine_of(sc, t * 1.3f + (float)foot_px) * 0.9f);
    int k = tree_at(size == 16 ? 0 : 1, density, big, bare, tiles_variant(x, y), sway);
    TreeDraw d;
    d.picture = &sc->trees[k];
    d.box = sc->tree_box[k];
    /* The foot near the bottom middle of the cell, the same in every cell. */
    d.foot_x = sx + (size / 2 - 1) * scale;
    d.foot_y = sy + (size - 2) * scale;
    d.x = d.foot_x - d.picture->foot_x * scale;
    d.y = d.foot_y - d.picture->foot_y * scale;
    return d;
}

/* A plant's tree colours: the crown from its colour lit and shaded, all turning
 * to straw as it withers; fruit shows only on a plant that offers it and holds
 * a whole one. */
static void tree_colours(const Sim *s, int i, uint32_t out[TREE_INKS])
{
    const World *w = &s->world;
    const Plant *p = &w->plant[i];
    Colour base = palette_plant(p);
    float straw = 0.0f;
    if (w->veg_wither[i] != 0) {
        int shade = palette_wither_shade(w->veg_wither[i], s->set.veg_wither_ticks);
        straw = (float)shade / (float)PALETTE_WITHER_SHADES;
    }
    Colour light = palette_mix(ramp_shade(base, 0.22f), PALETTE_WITHERED, straw);
    Colour crown = palette_mix(base, PALETTE_WITHERED, straw);
    Colour dark = palette_mix(ramp_shade(base, -0.4f), PALETTE_WITHERED, straw);
    Colour fleck = palette_mix(ramp_shade(base, -0.25f), PALETTE_WITHERED, straw);
    bool fruit = plant_offers_fruit(p) && p->fruit >= 1.0f;
    out[TREE_NONE] = 0;
    out[TREE_TRUNK_LIGHT] = to_pixel(TRUNK_LIGHT);
    out[TREE_TRUNK_DARK] = to_pixel(TRUNK_DARK);
    out[TREE_CROWN_LIGHT] = to_pixel(light);
    out[TREE_CROWN] = to_pixel(crown);
    out[TREE_CROWN_DARK] = to_pixel(dark);
    out[TREE_CROWN_FLECK] = to_pixel(fleck);
    out[TREE_FRUIT] = fruit ? to_pixel(PALETTE_FRUIT) : to_pixel(crown);
    out[TREE_BRANCH] = to_pixel(TRUNK_DARK);
}

/* One tree picture with its corner at (x, y), inside clip. Written straight
 * into the pixels when it lies wholly inside, as there are many trees. */
static void paint_tree(Canvas *c, const TreeDraw *d, int x, int y, int scale,
                       const uint32_t colours[TREE_INKS], CanvasClip clip)
{
    const TreeBox *b = &d->box;
    if (b->right < 0) {
        return; /* an empty picture */
    }
    int left = x + b->left * scale, top = y + b->top * scale;
    int right = x + (b->right + 1) * scale, bottom = y + (b->bottom + 1) * scale;
    if (right <= clip.x || bottom <= clip.y || left >= clip.x + clip.w ||
        top >= clip.y + clip.h) {
        return;
    }
    bool inside = left >= clip.x && top >= clip.y && right <= clip.x + clip.w &&
                  bottom <= clip.y + clip.h && left >= 0 && top >= 0 &&
                  right <= c->w && bottom <= c->h;
    const uint8_t *ink = d->picture->ink;
    for (int r = b->top; r <= b->bottom; r++) {
        for (int col = b->left; col <= b->right; col++) {
            uint8_t k = ink[r * TREE_W + col];
            if (k == TREE_NONE) {
                continue;
            }
            int px = x + col * scale, py = y + r * scale;
            if (!inside) {
                canvas_pixel(c, px, py, scale, colours[k], clip);
                continue;
            }
            for (int j = 0; j < scale; j++) {
                for (int i = 0; i < scale; i++) {
                    c->px[(py + j) * c->w + px + i] = colours[k];
                }
            }
        }
    }
}

/* Room for this many items; grown only when the view shows more cells. */
static bool grow_items(Scene *sc, int need)
{
    if (need <= sc->items_capacity) {
        return true;
    }
    SceneItem *items = realloc(sc->items, (size_t)need * sizeof(SceneItem));
    if (items == NULL) {
        return false;
    }
    sc->items = items;
    SceneItem *spare = realloc(sc->items_spare, (size_t)need * sizeof(SceneItem));
    if (spare == NULL) {
        return false; /* capacity unchanged: the next frame tries again */
    }
    sc->items_spare = spare;
    sc->items_capacity = need;
    return true;
}

/* The cells whose trees may show, along one axis: the visible ones, and where
 * the world does not fit, one more each side, as a crown reaches over its cell. */
static void tree_range(bool whole, int count, int *from, int *to)
{
    *from = whole ? 0 : -1;
    *to = whole ? count : count + 1; /* one past the last */
}

/* Every tree that may show, as items. */
static int add_trees(Scene *sc, const Camera *cam, const Sim *s, const WorldArea *area,
                     int view_x, int view_y, int n)
{
    int size = camera_sprite_size(cam);
    if (size == 0) {
        return n;
    }
    const World *w = &s->world;
    CameraCells v;
    camera_cells(cam, &v);
    int ppc = camera_ppc(cam);
    int scale = camera_sprite_scale(cam);
    int r0, r1, k0, k1;
    tree_range(area->whole_y, v.rows, &r0, &r1);
    tree_range(area->whole_x, v.cols, &k0, &k1);
    for (int r = r0; r < r1; r++) {
        for (int k = k0; k < k1; k++) {
            int i = world_idx(w, v.first_x + k, v.first_y + r);
            if (w->terrain[i] != TERRAIN_SOIL || w->veg[i] == 0) {
                continue;
            }
            int sx = view_x + v.screen_x + k * ppc;
            int sy = view_y + v.screen_y + r * ppc;
            /* Its foot, for sorting: the cell's bottom, the trunk's column. */
            sc->items[n++] = (SceneItem){.foot_y = sy + ppc,
                                         .foot_x = sx + (size / 2 - 1) * scale,
                                         .kind = SCENE_ITEM_TREE,
                                         .index = i};
        }
    }
    return n;
}

/* Every living body that may show, as items, its pose kept in sc->poses. */
static int add_bodies(Scene *sc, const Camera *cam, const Sim *s, float tick_fraction,
                      uint64_t body_ms, int view_x, int view_y, int n)
{
    int ppc = camera_ppc(cam);
    int count = s->pool.capacity < sc->anim.capacity ? s->pool.capacity
                                                     : sc->anim.capacity;
    for (int i = 0; i < count; i++) {
        const Creature *cr = &s->pool.items[i];
        if (!cr->alive || !sc->anim.slots[i].seen) {
            continue;
        }
        AnimPose *p = &sc->poses[i];
        anim_pose(&sc->anim, s, i, tick_fraction, body_ms, p);
        int sx, sy;
        camera_to_screen(cam, p->x, p->y, &sx, &sy);
        /* An ordinary body off the view is skipped before any more work; a
         * large one may reach in, which is decided picture by picture. */
        if (drawn_tiles(cr, s, p) == 1 &&
            (sx <= -ppc || sy <= -ppc || sx >= cam->view_w || sy >= cam->view_h)) {
            continue;
        }
        sc->items[n++] = (SceneItem){.foot_y = view_y + sy + ppc,
                                     .foot_x = view_x + sx + ppc / 2,
                                     .kind = SCENE_ITEM_BODY,
                                     .index = i};
    }
    return n;
}

/* Whether item a is drawn before item b: by foot row, then column, then index
 * (then kind, for a tree and a body that share an index), so two items on the
 * same row always come in the same order and never flicker. */
static bool item_before(const SceneItem *a, const SceneItem *b)
{
    if (a->foot_y != b->foot_y) {
        return a->foot_y < b->foot_y;
    }
    if (a->foot_x != b->foot_x) {
        return a->foot_x < b->foot_x;
    }
    if (a->index != b->index) {
        return a->index < b->index;
    }
    return a->kind < b->kind;
}

/* Sorts items[0 .. n - 1] back to front by insertion: each item moves left
 * past the ones it goes before. Quick on a list that is nearly in order. */
static void insertion_sort(SceneItem *items, int n)
{
    for (int k = 1; k < n; k++) {
        SceneItem item = items[k]; /* a struct is copied whole by = */
        int j = k - 1;
        while (j >= 0 && item_before(&item, &items[j])) {
            items[j + 1] = items[j];
            j--;
        }
        items[j + 1] = item;
    }
}

/*
 * Sorts the frame's items back to front: the trees (items[0 .. trees - 1],
 * added row by row, so already nearly in order) and the bodies (the rest, in
 * slot order) each by insertion, then the two lists merged through the spare
 * block. Not the C library's qsort: it may allocate memory for a large array,
 * and nothing is allocated per frame.
 */
static void sort_items(SceneItem *items, SceneItem *spare, int trees, int n)
{
    insertion_sort(items, trees);
    insertion_sort(items + trees, n - trees); /* items + trees: the bodies' start */
    int a = 0, b = trees, out = 0;
    while (a < trees || b < n) {
        bool take_a = b >= n || (a < trees && !item_before(&items[b], &items[a]));
        spare[out++] = take_a ? items[a++] : items[b++];
    }
    memcpy(items, spare, (size_t)n * sizeof(SceneItem));
}

/* The ellipse under a body: as wide as most of its tiles, two pixels high, its
 * bottom on the cell's bottom row and one below, as the body stands there. */
static void body_shadow(Canvas *c, const Camera *cam, const Sim *s, const Scene *sc,
                        int slot, const SceneItem *item, const WorldArea *area)
{
    int size = camera_sprite_size(cam);
    int scale = camera_sprite_scale(cam);
    const Creature *cr = &s->pool.items[slot];
    int tiles = drawn_tiles(cr, s, &sc->poses[slot]);
    int rx = tiles * size * 3 / 8 * scale;
    int ry = (size / 8) * scale;
    int cy = item->foot_y - ry;
    int dx[9], dy[9];
    int copies = tiles > 1 ? copy_offsets(area, dx, dy) : 1;
    for (int k = 0; k < copies; k++) {
        int ox = tiles > 1 ? dx[k] : 0, oy = tiles > 1 ? dy[k] : 0;
        canvas_darken_ellipse(c, item->foot_x + ox, cy + oy, rx, ry, TREE_SHADOW_KEEP,
                              area->clip);
    }
}

/* Draws the frame's trees and bodies: their shadows first, then each from the
 * back row to the front. */
static void draw_items(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                       float tick_fraction, uint64_t water_ms, uint64_t body_ms,
                       int view_x, int view_y)
{
    CameraCells v;
    camera_cells(cam, &v);
    int count = s->pool.capacity < sc->anim.capacity ? s->pool.capacity
                                                     : sc->anim.capacity;
    if (!grow_items(sc, count + (v.cols + 2) * (v.rows + 2))) {
        return; /* out of memory: no trees or bodies this frame */
    }
    WorldArea area = world_area(cam, view_x, view_y);
    int trees = add_trees(sc, cam, s, &area, view_x, view_y, 0);
    int n = add_bodies(sc, cam, s, tick_fraction, body_ms, view_x, view_y, trees);
    sort_items(sc->items, sc->items_spare, trees, n);

    int size = camera_sprite_size(cam);
    int scale = camera_sprite_scale(cam);
    int ppc = camera_ppc(cam);
    int season = years_season_of(s->tick, s->set.ticks_per_year);
    float t = ambient_seconds(water_ms);
    int dx[9], dy[9];
    int copies = copy_offsets(&area, dx, dy);
    if (size > 0) {
        for (int k = 0; k < n; k++) {
            const SceneItem *it = &sc->items[k];
            if (it->kind == SCENE_ITEM_BODY) {
                body_shadow(c, cam, s, sc, it->index, it, &area);
                continue;
            }
            TreeDraw d = tree_draw(sc, s, it->index, it->foot_x - (size / 2 - 1) * scale,
                                   it->foot_y - ppc, size, scale, season, t);
            /* v9 puts the shadow 2 pixels right and 1 down of the foot. */
            int cx = d.foot_x + (2 * size / 16) * scale;
            int cy = d.foot_y + scale;
            for (int j = 0; j < copies; j++) {
                canvas_darken_ellipse(c, cx + dx[j], cy + dy[j],
                                      d.picture->shadow_rx * scale,
                                      d.picture->shadow_ry * scale, TREE_SHADOW_KEEP,
                                      area.clip);
            }
        }
    }
    for (int k = 0; k < n; k++) {
        const SceneItem *it = &sc->items[k];
        if (it->kind == SCENE_ITEM_TREE) {
            TreeDraw d = tree_draw(sc, s, it->index, it->foot_x - (size / 2 - 1) * scale,
                                   it->foot_y - ppc, size, scale, season, t);
            uint32_t colours[TREE_INKS];
            tree_colours(s, it->index, colours);
            for (int j = 0; j < copies; j++) {
                paint_tree(c, &d, d.x + dx[j], d.y + dy[j], scale, colours, area.clip);
            }
            continue;
        }
        const Creature *cr = &s->pool.items[it->index];
        const AnimPose *p = &sc->poses[it->index];
        int tiles = drawn_tiles(cr, s, p);
        int sx = it->foot_x - ppc / 2, sy = it->foot_y - ppc;
        if (tiles > 1) {
            draw_large(sc, c, cam, s, cr, p, tiles, sx, sy, &area);
        } else if (size == 0) {
            canvas_rect(c, sx, sy, ppc, ppc, to_pixel(creature_colour(cr)));
        } else {
            draw_body(sc, c, cam, s, cr, it->index, p, sx, sy);
        }
    }
}

void scene_draw_world(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                      float tick_fraction, uint64_t water_ms, uint64_t body_ms,
                      int view_x, int view_y)
{
    if (cam->view_w == 0 || cam->view_h == 0) {
        return;
    }
    place_ground(sc, c, cam, s, water_ms, view_x, view_y);
    draw_overlays(sc, c, cam, s, water_ms, view_x, view_y);
    draw_items(sc, c, cam, s, tick_fraction, water_ms, body_ms, view_x, view_y);
    draw_marks(sc, c, cam, s, tick_fraction, body_ms, view_x, view_y);
}

/* Shares the panel's rows between its text and the bodies below it. */
static void split_panel(Layout *l, int rows)
{
    int spare = rows - SCENE_PANEL_MIN_ROWS;
    l->show_enlarged = spare >= SCENE_ENLARGED_ROWS;
    l->show_strip = spare >= SCENE_ENLARGED_ROWS + SCENE_STRIP_ROWS;
    l->family_rows = (l->show_enlarged ? SCENE_ENLARGED_ROWS : 0) +
                     (l->show_strip ? SCENE_STRIP_ROWS : 0);
    l->panel_rows = rows - l->family_rows;
    l->family_y = l->panel_y + l->panel_rows * FONT_H;
}

void scene_layout(Layout *l, int window_w, int window_h)
{
    *l = (Layout){0}; /* every field not named below becomes 0 or false */
    if (window_w < SCENE_MIN_W || window_h < SCENE_MIN_H) {
        l->too_small = true;
        return;
    }
    l->has_panel = window_w >= SCENE_MIN_W + SCENE_PANEL_W;
    l->view_y = SCENE_TEXT_H;
    l->view_w = l->has_panel ? window_w - SCENE_PANEL_W : window_w;
    l->view_h = window_h - 2 * SCENE_TEXT_H;
    l->help_y = window_h - SCENE_TEXT_H;
    if (l->has_panel) {
        l->panel_x = l->view_w;
        l->panel_y = SCENE_TEXT_H;
        split_panel(l, l->view_h / FONT_H);
    }
}

PanelPick scene_panel_pick(const Layout *l, int x, int y)
{
    if (!l->has_panel || x < l->panel_x || x >= l->panel_x + SCENE_PANEL_W ||
        y < l->panel_y) {
        return PANEL_PICK_NONE;
    }
    if (l->show_enlarged && y >= l->family_y &&
        y < l->family_y + SCENE_ENLARGED_ROWS * FONT_H) {
        return PANEL_PICK_LARGEST_FAMILY;
    }
    /* A row that did not fit is not on screen, so it cannot be clicked. */
    int row = (y - l->panel_y) / FONT_H;
    if (row >= l->panel_rows) {
        return PANEL_PICK_NONE;
    }
    if (row == PANEL_ROW_TOP_HUNTER) {
        return PANEL_PICK_TOP_HUNTER;
    }
    return row == PANEL_ROW_OLDEST ? PANEL_PICK_OLDEST : PANEL_PICK_NONE;
}

void scene_click(Inspect *in, const Sim *s, float wx, float wy)
{
    const World *w = &s->world;
    int cx = (int)wx; /* wx and wy are inside the world: never negative */
    int cy = (int)wy;
    int best_x = 0, best_y = 0, best_distance = -1;
    for (int dy = -2; dy <= 2; dy++) {
        for (int dx = -2; dx <= 2; dx++) {
            int distance = dx * dx + dy * dy;
            /* world_idx wraps, so a cell over the edge is found too. */
            if (w->occupant[world_idx(w, cx + dx, cy + dy)] >= 0 &&
                (best_distance < 0 || distance < best_distance)) {
                best_distance = distance;
                best_x = cx + dx;
                best_y = cy + dy;
            }
        }
    }
    if (best_distance < 0) {
        inspect_stop(in);
        return;
    }
    /* Into the world: % keeps the sign of a negative number, so add w first. */
    int x = (best_x + w->w) % w->w;
    int y = (best_y + w->h) % w->h;
    in->on = true;
    inspect_move(in, s, x - in->x, y - in->y, w->w, w->h);
}

const Creature *scene_follow(const Scene *sc, Camera *cam, Inspect *in,
                             const Sim *s, float tick_fraction)
{
    inspect_update(in, s);
    const Creature *cr = inspect_creature(in, s);
    if (cr == NULL) {
        return NULL;
    }
    /* Subtracting two pointers into one array gives the distance in elements,
     * not bytes: this is the creature's slot. */
    int slot = (int)(cr - s->pool.items);
    if (slot < sc->anim.capacity && sc->anim.slots[slot].seen) {
        AnimPose p;
        anim_pose(&sc->anim, s, slot, tick_fraction, 0, &p);
        camera_centre_on(cam, p.x, p.y);
    }
    return cr;
}

void scene_draw_cursor(const Scene *sc, Canvas *c, const Camera *cam,
                       const Sim *s, const Creature *followed,
                       float tick_fraction, int view_x, int view_y)
{
    if (followed == NULL || cam->view_w == 0 || cam->view_h == 0) {
        return;
    }
    int slot = (int)(followed - s->pool.items);
    if (slot >= sc->anim.capacity || !sc->anim.slots[slot].seen) {
        return;
    }
    AnimPose p;
    anim_pose(&sc->anim, s, slot, tick_fraction, 0, &p);
    int sx, sy;
    camera_to_screen(cam, p.x, p.y, &sx, &sy);
    int ppc = camera_ppc(cam);
    /* Round the whole picture: one cell, or a large body's tiles. */
    int tiles = drawn_tiles(followed, s, &p);
    int bx = sx, by = sy;
    if (tiles > 1) {
        large_corner(sx, sy, ppc, tiles, &bx, &by);
    }
    int extent = tiles * ppc;
    int left = view_x + bx - 2;
    int top = view_y + by - 2;
    int right = view_x + bx + extent + 1; /* the last column of the frame */
    int bottom = view_y + by + extent + 1;
    int len = ppc < 8 ? 2 : ppc / 4;
    uint32_t white = to_pixel(WHITE);
    /* Each corner is two short lines. */
    canvas_rect(c, left, top, len, 1, white);
    canvas_rect(c, left, top, 1, len, white);
    canvas_rect(c, right - len + 1, top, len, 1, white);
    canvas_rect(c, right, top, 1, len, white);
    canvas_rect(c, left, bottom, len, 1, white);
    canvas_rect(c, left, bottom - len + 1, 1, len, white);
    canvas_rect(c, right - len + 1, bottom, len, 1, white);
    canvas_rect(c, right, bottom - len + 1, 1, len, white);
}

/* One name under the body in this slot, or nothing if it would leave the view. */
static void draw_name(const Scene *sc, Canvas *c, const Camera *cam,
                      const Sim *s, int slot, float tick_fraction, int view_x,
                      int view_y)
{
    if (slot >= sc->anim.capacity || !sc->anim.slots[slot].seen) {
        return;
    }
    const Creature *cr = &s->pool.items[slot];
    AnimPose p;
    anim_pose(&sc->anim, s, slot, tick_fraction, 0, &p);
    int sx, sy;
    camera_to_screen(cam, p.x, p.y, &sx, &sy);
    int ppc = camera_ppc(cam);
    char name[16];
    name_from_id(cr->id, name, sizeof(name));
    /* strlen counts bytes, which for these ASCII names is also characters.
     * It returns a size_t; the name is under 16, so the int conversion is safe. */
    int w = (int)strlen(name) * FONT_W;
    /* Centred under its own cell, the cell its feet stand on even when large. */
    int x = sx + ppc / 2 - w / 2;
    int y = sy + ppc + 1;
    if (x < 0 || y < 0 || x + w > cam->view_w || y + FONT_H > cam->view_h) {
        return;
    }
    font_text(c, view_x + x, view_y + y, name, to_pixel(WHITE),
              to_pixel(BACKGROUND));
}

void scene_draw_names(const Scene *sc, Canvas *c, const Camera *cam,
                      const Sim *s, const Creature *followed,
                      float tick_fraction, int view_x, int view_y)
{
    if (camera_sprite_size(cam) != 16) {
        return; /* Far and World: bodies of 8 pixels or less, names would bury them */
    }
    int followed_slot = followed != NULL ? (int)(followed - s->pool.items) : -1;
    int n = s->pool.capacity < sc->anim.capacity ? s->pool.capacity
                                                 : sc->anim.capacity;
    for (int i = 0; i < n; i++) {
        const Creature *cr = &s->pool.items[i];
        /* & tests one bit of the flags; && would only ask whether both sides
         * are non-zero. */
        if (cr->alive && i != followed_slot && (cr->tally.flags & TALLY_GIANT)) {
            draw_name(sc, c, cam, s, i, tick_fraction, view_x, view_y);
        }
    }
    /* Last, so the followed creature's name is never covered by another. */
    if (followed_slot >= 0) {
        draw_name(sc, c, cam, s, followed_slot, tick_fraction, view_x, view_y);
    }
}

/* How long a family's average body breathes in and out, in milliseconds. */
#define SAMPLE_BREATH_MS 600u

void scene_draw_followed(Scene *sc, Canvas *c, const Sim *s,
                         const Creature *followed, uint64_t now_ms, int x, int y)
{
    int slot = (int)(followed - s->pool.items);
    if (slot >= sc->anim.capacity || !sc->anim.slots[slot].seen) {
        return;
    }
    AnimPose p;
    anim_pose(&sc->anim, s, slot, 0.0f, now_ms, &p);
    const SpriteMask *m =
        sprite_get(&sc->sprites, slot, followed->id, &followed->genome, 16, p.frame);
    if (m == NULL) {
        return;
    }
    int scale = SCENE_BODY_BIG / 16;
    canvas_rect(c, x, y, SCENE_BODY_BIG, SCENE_BODY_BIG, to_pixel(BACKGROUND));
    canvas_mask(c, m->body, 16, x, y, scale, p.flip,
                to_pixel(creature_colour(followed)));
    canvas_mask(c, m->white, 16, x, y, scale, p.flip, to_pixel(WHITE));
    uint64_t rows[16];
    for (int j = 0; j < 16; j++) {
        rows[j] = m->body[j];
    }
    draw_scars(c, rows, 16, x, y, scale, p.flip,
               recent_hit_count(followed, s->tick), whole(c));
}

/* One sample's body in this frame, at a scale, on the near-black background. */
static void draw_sample_body(Canvas *c, const FamilySample *sample, int frame,
                             int x, int y, int scale)
{
    SpriteMask m;
    sprite_build(&m, &sample->traits, 16, frame);
    canvas_rect(c, x, y, 16 * scale, 16 * scale, to_pixel(BACKGROUND));
    canvas_mask(c, m.body, 16, x, y, scale, false,
                to_pixel(palette_creature(sample->hue, sample->meat_share)));
    canvas_mask(c, m.white, 16, x, y, scale, false, to_pixel(WHITE));
}

void scene_draw_sample(Canvas *c, const FamilySample *sample, uint64_t now_ms,
                       int x, int y)
{
    int frame = FRAME_REST0 + (int)((now_ms / SAMPLE_BREATH_MS) % 2u);
    draw_sample_body(c, sample, frame, x, y, SCENE_BODY_BIG / 16);
}

void scene_draw_strip(Canvas *c, const Families *f, uint32_t lineage, int x,
                      int y, int w)
{
    FamilySample samples[FAMILY_STRIP];
    int n = family_strip(f, lineage, samples);
    for (int i = 0; i < n && (i + 1) * 16 <= w; i++) {
        draw_sample_body(c, &samples[i], FRAME_REST0, x + i * 16, y, 1);
    }
}

/* One line in the box, cut to the columns left inside the area. */
static void over_line(Canvas *c, const char *text, int x, int y, int bottom,
                      int cols, Colour fg)
{
    if (y + FONT_H > bottom || cols <= 0) {
        return;
    }
    /* font_text stops at the terminator, so cutting means copying: the precision
     * in "%.*s" limits how much of text is printed, and takes its value, an int,
     * from the argument list. */
    char cut[APP_OVER_COLS + 32];
    snprintf(cut, sizeof(cut), "%.*s", cols, text);
    font_text(c, x, y, cut, to_pixel(fg), to_pixel(PALETTE_TEXT_BACKGROUND));
}

void scene_draw_game_over(Canvas *c, const Sim *s, const char *keys, int x, int y,
                          int w, int h)
{
    char lines[APP_OVER_LINES][APP_OVER_COLS];
    int n = app_game_over_lines(s, lines);
    int longest = (int)strlen(keys);
    for (int i = 0; i < n; i++) {
        int len = (int)strlen(lines[i]);
        longest = len > longest ? len : longest;
    }
    /* One empty row above, below, and before the keys; one empty column each side. */
    int box_w = (longest + 2) * FONT_W;
    int box_h = (n + 4) * FONT_H;
    box_w = box_w < w ? box_w : w;
    box_h = box_h < h ? box_h : h;
    if (box_w <= 0 || box_h <= 0) {
        return;
    }
    int left = x + (w - box_w) / 2;
    int top = y + (h - box_h) / 2;
    int bottom = top + box_h;
    canvas_rect(c, left, top, box_w, box_h, to_pixel(PALETTE_TEXT_BACKGROUND));
    int cols = box_w / FONT_W - 2;
    int text_x = left + FONT_W;
    for (int i = 0; i < n; i++) {
        over_line(c, lines[i], text_x, top + (i + 1) * FONT_H, bottom, cols,
                  PALETTE_TEXT);
    }
    over_line(c, keys, text_x, top + (n + 2) * FONT_H, bottom, cols, PALETTE_TEXT);
}
