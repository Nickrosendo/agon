#include "scene.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* strlen */

#include "app.h"
#include "chronicle.h" /* name_from_id */
#include "climate.h" /* climate_region, climate_region_hue */
#include "palette.h"

/* How long one water frame lasts, in milliseconds. */
#define WATER_FRAME_MS 250u
/* How long one frame of a fight's or a meal's mark lasts. */
#define MARK_FRAME_MS 110u

/* The near-black behind every symbol; outside the world it is true black. */
static const Colour BACKGROUND = {6, 6, 8};
/* The second colour of a creature: teeth and spots. */
static const Colour WHITE = {245, 240, 235};
/* A little carrion is a pale bone; a lot goes dark red. */
static const Colour BONE = {196, 176, 150};
/* The flash of a fight: brighter than anything else in the world. */
static const Colour SLASH = {255, 70, 50};
/* Crumbs of meat. Crumbs of plant are the plant's own colour, lightened, or
 * they would vanish against the tree they come from. */
static const Colour MEAT_CRUMB = {255, 110, 100};
/* A cut not healed yet: a pale pink, to be seen on any family's colour. */
static const Colour SCAR = {255, 170, 160};

/* How far a region's colour tints the near-black behind a symbol, and the flat
 * colour of a cell at the furthest zoom. Small, so the ground stays ground. */
#define REGION_TINT 0.14f
#define REGION_TINT_FLAT 0.12f
/* The pale saturation plant-eaters are drawn in, so a body that matches its
 * region looks like that region's tint, only brighter. */
#define REGION_SATURATION 0.55f

static uint32_t to_pixel(Colour c)
{
    return canvas_rgb(c.r, c.g, c.b);
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
    /* Sized by the first frame. An empty canvas allocates nothing, so this
     * cannot fail today; checked anyway, like every other allocation. */
    if (!canvas_init(&sc->ground, 0, 0)) {
        sprite_cache_free(&sc->sprites);
        anim_free(&sc->anim);
        return false;
    }
    sc->ground_valid = false;
    /* Every symbol once, here, so drawing a frame only copies bits. */
    for (int z = 0; z < 2; z++) {
        int size = z == 0 ? 16 : 8;
        for (int f = 0; f < TILE_MARK_FRAMES; f++) {
            tiles_build(sc->slash[z][f], TILE_SLASH, 0, 0, size, f);
            tiles_build(sc->crumbs[z][f], TILE_CRUMBS, 0, 0, size, f);
        }
        for (int v = 0; v < TILE_VARIANTS; v++) {
            for (int f = 0; f < TILE_WATER_FRAMES; f++) {
                tiles_build(sc->water[z][v][f], TILE_WATER, v, 0, size, f);
                tiles_build(sc->rain[z][v][f], TILE_RAIN, v, 0, size, f);
            }
            tiles_build(sc->rock[z][v], TILE_ROCK, v, 0, size, 0);
            for (int d = 0; d < TILE_SOIL_DETAILS; d++) {
                tiles_build(sc->soil[z][v][d], TILE_SOIL, v, d, size, 0);
            }
            for (int d = 0; d <= VEG_MAX; d++) {
                tiles_build(sc->plant[z][v][d], TILE_PLANT, v, d, size, 0);
            }
            tiles_build(sc->carrion[z][v], TILE_CARRION, v, 0, size, 0);
        }
    }
    return true;
}

void scene_free(Scene *sc)
{
    sprite_cache_free(&sc->sprites);
    anim_free(&sc->anim);
    canvas_free(&sc->ground);
}

void scene_reset(Scene *sc)
{
    sprite_cache_reset(&sc->sprites);
    anim_reset(&sc->anim);
    sc->ground_valid = false; /* a new world can have the same tick number */
}

void scene_update(Scene *sc, const Sim *s, int ticks_per_second)
{
    anim_update(&sc->anim, s, ticks_per_second);
}

int scene_water_frame(uint64_t water_ms)
{
    /* One clock for all water; each variant was drawn at its own phase. */
    return (int)((water_ms / WATER_FRAME_MS) % TILE_WATER_FRAMES);
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

/* A cell as one flat colour, for the furthest zoom. */
static Colour flat_ground(const Sim *s, int i)
{
    const World *w = &s->world;
    if (w->terrain[i] == TERRAIN_WATER) {
        return PALETTE_WATER;
    }
    if (w->terrain[i] == TERRAIN_SHALLOW) {
        return PALETTE_SHALLOW;
    }
    if (w->terrain[i] == TERRAIN_ROCK) {
        return PALETTE_ROCK;
    }
    if (w->carrion[i] > 0.0f) {
        return carrion_colour(s, i);
    }
    if (w->veg[i] > 0) {
        float t = (float)(w->veg[i] + 1) / (float)(VEG_MAX + 1);
        return palette_mix(BACKGROUND, plant_colour(s, i), t);
    }
    return palette_mix(BACKGROUND, palette_ground(w->fertility[i], w->water[i]),
                       0.25f);
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

/* The soil's dot density, 0 .. TILE_SOIL_DETAILS - 1, from its fertility. */
static int soil_detail(float fertility)
{
    int d = (int)(fertility * (float)TILE_SOIL_DETAILS);
    return d >= TILE_SOIL_DETAILS ? TILE_SOIL_DETAILS - 1 : d;
}

/* One cell as a symbol, on the background already there. */
static void draw_tile(Scene *sc, Canvas *c, const Sim *s, int x, int y, int sx,
                      int sy, int z, int size, int scale, int water_frame)
{
    const World *w = &s->world;
    int i = y * w->w + x; /* x and y are already inside the world */
    int v = tiles_variant(x, y);
    const uint16_t *rows;
    Colour colour;
    if (w->terrain[i] == TERRAIN_WATER) {
        rows = sc->water[z][v][water_frame];
        colour = PALETTE_WATER;
    } else if (w->terrain[i] == TERRAIN_SHALLOW) {
        rows = sc->water[z][v][water_frame];
        colour = PALETTE_SHALLOW;
    } else if (w->terrain[i] == TERRAIN_ROCK) {
        rows = sc->rock[z][v];
        colour = PALETTE_ROCK;
    } else if (w->carrion[i] > 0.0f) {
        rows = sc->carrion[z][v];
        colour = carrion_colour(s, i);
    } else if (w->veg[i] > 0) {
        rows = sc->plant[z][v][w->veg[i]];
        colour = plant_colour(s, i);
    } else {
        rows = sc->soil[z][v][soil_detail(w->fertility[i])];
        colour = palette_ground(w->fertility[i], w->water[i]);
    }
    /* The region shows in the gaps between the symbol's pixels. The rest of
     * the view was filled with BACKGROUND already, so only cells with a region
     * need this rectangle. */
    Colour region;
    if (region_colour(s, i, &region)) {
        canvas_rect(c, sx, sy, size * scale, size * scale,
                    to_pixel(palette_mix(BACKGROUND, region, REGION_TINT)));
    }
    canvas_mask(c, rows, size, sx, sy, scale, false, to_pixel(colour));
    /* Harder rain falls on more cells: variant 0 shows any rain, variant 3 only
     * the heaviest. The variant is a hash of the place, so this draws no random
     * number from the simulation. */
    if (w->rain[i] * (float)TILE_VARIANTS > (float)v) {
        canvas_mask(c, sc->rain[z][v][water_frame], size, sx, sy, scale, false,
                    to_pixel(PALETTE_RAIN));
    }
}

/* Every cell the camera sees. */
static void draw_terrain(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                         uint64_t water_ms, int view_x, int view_y)
{
    const World *w = &s->world;
    CameraCells v;
    camera_cells(cam, &v);
    int ppc = camera_ppc(cam);
    int size = camera_sprite_size(cam);
    int scale = camera_sprite_scale(cam);
    int z = size == 16 ? 0 : 1;
    int water_frame = scene_water_frame(water_ms);
    for (int r = 0; r < v.rows; r++) {
        int y = (v.first_y + r) % w->h;
        int sy = view_y + v.screen_y + r * ppc;
        for (int k = 0; k < v.cols; k++) {
            int x = (v.first_x + k) % w->w;
            int sx = view_x + v.screen_x + k * ppc;
            if (size == 0) {
                Colour flat = flat_colour(s, y * w->w + x);
                canvas_rect(c, sx, sy, ppc, ppc, to_pixel(flat));
            } else {
                draw_tile(sc, c, s, x, y, sx, sy, z, size, scale, water_frame);
            }
        }
    }
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

/* The whole canvas, for drawing that only the canvas's edges need to stop. */
static CanvasClip whole(const Canvas *c)
{
    return (CanvasClip){0, 0, c->w, c->h};
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

static void draw_creatures(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                           float tick_fraction, uint64_t body_ms, int view_x,
                           int view_y)
{
    int ppc = camera_ppc(cam);
    int size = camera_sprite_size(cam);
    int n = s->pool.capacity < sc->anim.capacity ? s->pool.capacity
                                                 : sc->anim.capacity;
    WorldArea area = world_area(cam, view_x, view_y);
    /* Smaller bodies first, so a large one stands in front of those it
     * covers. Each pass skips the bodies of the other sizes. */
    for (int tier = 1; tier <= SPRITE_MAX_TILES; tier++) {
        for (int i = 0; i < n; i++) {
            const Creature *cr = &s->pool.items[i];
            if (!cr->alive || !sc->anim.slots[i].seen) {
                continue;
            }
            AnimPose p;
            anim_pose(&sc->anim, s, i, tick_fraction, body_ms, &p);
            int tiles = drawn_tiles(cr, s, &p);
            if (tiles != tier) {
                continue;
            }
            int sx, sy;
            camera_to_screen(cam, p.x, p.y, &sx, &sy);
            if (tiles > 1) {
                /* Off the view or not, decided picture by picture inside. */
                draw_large(sc, c, cam, s, cr, &p, tiles, view_x + sx,
                           view_y + sy, &area);
                continue;
            }
            /* Off the view: skipped before any work. */
            if (sx <= -ppc || sy <= -ppc || sx >= cam->view_w ||
                sy >= cam->view_h) {
                continue;
            }
            sx += view_x;
            sy += view_y;
            if (size == 0) {
                canvas_rect(c, sx, sy, ppc, ppc, to_pixel(creature_colour(cr)));
            } else {
                draw_body(sc, c, cam, s, cr, i, &p, sx, sy);
            }
        }
    }
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

/* True when the ground layer already shows what this frame needs. */
static bool ground_is_current(const Scene *sc, const Camera *cam, const Sim *s,
                              int water)
{
    return sc->ground_valid && sc->ground.w == cam->view_w &&
           sc->ground.h == cam->view_h && sc->ground_tick == s->tick &&
           sc->ground_water == water && sc->ground_level == cam->level &&
           sc->ground_cx == cam->cx && sc->ground_cy == cam->cy;
}

/* Draws the view's ground, black around a world smaller than the view, at
 * (x, y). Both callers below need exactly this. */
static void draw_ground(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                        uint64_t water_ms, int x, int y)
{
    /* True black for the whole view, near-black where the world is. */
    canvas_rect(c, x, y, cam->view_w, cam->view_h, canvas_rgb(0, 0, 0));
    CameraCells v;
    camera_cells(cam, &v);
    int ppc = camera_ppc(cam);
    canvas_rect(c, x + v.screen_x, y + v.screen_y, v.cols * ppc, v.rows * ppc,
                to_pixel(BACKGROUND));
    draw_terrain(sc, c, cam, s, water_ms, x, y);
}

/* The ground into the frame, redrawn only when something in it has changed. */
static void place_ground(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                         uint64_t water_ms, int view_x, int view_y)
{
    int water = scene_water_frame(water_ms);
    if (ground_is_current(sc, cam, s, water)) {
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
    draw_ground(sc, &sc->ground, cam, s, water_ms, 0, 0);
    sc->ground_valid = true;
    sc->ground_tick = s->tick;
    sc->ground_water = water;
    sc->ground_level = cam->level;
    sc->ground_cx = cam->cx;
    sc->ground_cy = cam->cy;
    canvas_copy(c, &sc->ground, view_x, view_y);
}

void scene_draw_world(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                      float tick_fraction, uint64_t water_ms, uint64_t body_ms,
                      int view_x, int view_y)
{
    if (cam->view_w == 0 || cam->view_h == 0) {
        return;
    }
    place_ground(sc, c, cam, s, water_ms, view_x, view_y);
    draw_creatures(sc, c, cam, s, tick_fraction, body_ms, view_x, view_y);
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
