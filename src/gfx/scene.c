#include "scene.h"

#include "palette.h"

/* How long one water frame lasts, in milliseconds. */
#define WATER_FRAME_MS 250u

/* The near-black behind every symbol; outside the world it is true black. */
static const Colour BACKGROUND = {6, 6, 8};
/* The second colour of a creature: teeth and spots. */
static const Colour WHITE = {245, 240, 235};
/* A little carrion is a pale bone; a lot goes dark red. */
static const Colour BONE = {196, 176, 150};

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
    /* Every symbol once, here, so drawing a frame only copies bits. */
    for (int z = 0; z < 2; z++) {
        int size = z == 0 ? 16 : 8;
        for (int v = 0; v < TILE_VARIANTS; v++) {
            for (int f = 0; f < TILE_WATER_FRAMES; f++) {
                tiles_build(sc->water[z][v][f], TILE_WATER, v, 0, size, f);
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
}

void scene_reset(Scene *sc)
{
    sprite_cache_reset(&sc->sprites);
    anim_reset(&sc->anim);
}

void scene_update(Scene *sc, const Sim *s, int ticks_per_second)
{
    anim_update(&sc->anim, s, ticks_per_second);
}

/* A plant's colour, turning to straw as it withers. */
static Colour plant_colour(const Sim *s, int i)
{
    const World *w = &s->world;
    Colour plant = palette_plant(w->veg_rule[i]);
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

/* A cell as one flat colour, for the furthest zoom. */
static Colour flat_colour(const Sim *s, int i)
{
    const World *w = &s->world;
    if (w->terrain[i] == TERRAIN_WATER) {
        return PALETTE_WATER;
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
    return palette_mix(BACKGROUND, palette_soil(w->fertility[i]), 0.25f);
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
        colour = palette_soil(w->fertility[i]);
    }
    canvas_mask(c, rows, size, sx, sy, scale, false, to_pixel(colour));
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
    /* One clock for all water; each variant was drawn at its own phase. */
    int water_frame = (int)((water_ms / WATER_FRAME_MS) % TILE_WATER_FRAMES);
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

/* One creature as a symbol, its tile cleared first so its eye shows black
 * and no plant shows through. A newborn is drawn at half size at first. */
static void draw_body(Scene *sc, Canvas *c, const Camera *cam,
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
}

static void draw_creatures(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                           float tick_fraction, uint64_t body_ms, int view_x,
                           int view_y)
{
    int ppc = camera_ppc(cam);
    int size = camera_sprite_size(cam);
    int n = s->pool.capacity < sc->anim.capacity ? s->pool.capacity
                                                 : sc->anim.capacity;
    for (int i = 0; i < n; i++) {
        const Creature *cr = &s->pool.items[i];
        if (!cr->alive || !sc->anim.slots[i].seen) {
            continue;
        }
        AnimPose p;
        anim_pose(&sc->anim, s, i, tick_fraction, body_ms, &p);
        int sx, sy;
        camera_to_screen(cam, p.x, p.y, &sx, &sy);
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
            draw_body(sc, c, cam, cr, i, &p, sx, sy);
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
    /* True black for the whole view, near-black where the world is. */
    canvas_rect(c, view_x, view_y, cam->view_w, cam->view_h,
                canvas_rgb(0, 0, 0));
    CameraCells v;
    camera_cells(cam, &v);
    int ppc = camera_ppc(cam);
    canvas_rect(c, view_x + v.screen_x, view_y + v.screen_y, v.cols * ppc,
                v.rows * ppc, to_pixel(BACKGROUND));
    draw_terrain(sc, c, cam, s, water_ms, view_x, view_y);
    draw_creatures(sc, c, cam, s, tick_fraction, body_ms, view_x, view_y);
}
