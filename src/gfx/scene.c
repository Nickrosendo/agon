#include "scene.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* strlen */

#include "app.h"
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
        Colour plant = palette_plant(w->veg_rule[p->target_y * w->w + p->target_x]);
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
    int left = view_x + sx - 2;
    int top = view_y + sy - 2;
    int right = view_x + sx + ppc + 1; /* the last column of the frame */
    int bottom = view_y + sy + ppc + 1;
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
