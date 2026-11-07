/*
 * scene: draws one frame of the world into a canvas, from the simulation,
 * the camera, the sprites and tiles and the animation memory. Reads the
 * simulation only: no writes through the Sim, and none of its random numbers.
 */
#ifndef AGON_SCENE_H
#define AGON_SCENE_H

#include <stdbool.h>
#include <stdint.h>

#include "anim.h"
#include "camera.h"
#include "canvas.h"
#include "family.h"
#include "font.h"
#include "inspect.h"
#include "panel.h"
#include "particles.h"
#include "sim.h"
#include "ramp.h"
#include "sprite.h"
#include "tiles.h"

/* The overlays' clock step: glints, foam, sway, rain. While paused they are
 * what moves, so the window redraws once a step. */
#define SCENE_AMBIENT_MS 100u
/* The ground layer's water drift step: the slow ripple is redrawn this often. */
#define SCENE_DRIFT_MS 500u
/* How long the soil's colours may lag behind the world, in window time: the
 * soil changes so slowly that a second is never seen (controller's call). */
#define SCENE_COLOUR_MS 1000u
/* The number of tree pictures: 2 sizes, VEG_MAX densities, big or not, bare or
 * not, TILE_VARIANTS variants and 3 sways. */
#define SCENE_TREES (2 * VEG_MAX * 2 * 2 * TILE_VARIANTS * 3)

/* What is drawn back to front: a tree or a body, standing at (foot_x, foot_y)
 * on the canvas. index is the tree's cell or the body's slot. */
enum { SCENE_ITEM_TREE, SCENE_ITEM_BODY };
typedef struct {
    int32_t foot_y, foot_x;
    int16_t kind;
    int32_t index;
} SceneItem;

/* The part of a tree picture its ink fills; right and bottom are the last. */
typedef struct {
    int8_t left, top, right, bottom;
} TreeBox;

/* How many steps the water's sine table has: a power of two, so a step number
 * is wrapped into the table with & (SCENE_SINE_STEPS - 1). */
#define SCENE_SINE_STEPS 1024

/* One cell's ground colours as pixels: [EDGE_NONE] the plain three steps, each
 * other edge's three worked out the first time a pixel needs them (bit e of
 * done says edge e is ready), as most cells use only one or two edges. And what
 * they were worked out from: if a cell's ground is unchanged at the next
 * redraw (most of it changes only every few dozen ticks), they are kept. */
typedef struct {
    uint32_t pixel[EDGE_KINDS][3];
    uint32_t done;
    bool known; /* false: worked out from nothing yet */
    int cell, terrain, region;
    float fertility, water, snow;
} GroundPixels;

/* Index 0 of each table's first dimension is the 16-pixel size, 1 the
 * 8-pixel size. These are arrays of arrays, all one block inside the Scene:
 * rain[s][v][f] is a row table of 16 numbers, with no pointers to follow. */
typedef struct {
    /* The creatures' pictures, kept picture by picture (sprite.h). */
    SpriteCache sprites;
    /* Per pool slot: its creature's colours, and the id and species they were
     * made for (palette_species PALETTE_NONE: not made yet); they are made
     * again when either changes. And the body each slot is drawn as this frame. */
    SpritePalette *palettes;
    uint32_t *palette_id;
    uint8_t *palette_species;
    SpriteBody *bodies;
    int slots;
    Anim anim;
    /* Dust, leaves, blood, fur and z's: a pool inside the Scene (40 KB), so
     * nothing is allocated for them. */
    Particles particles;
    uint16_t rain[2][TILE_VARIANTS][TILE_RAIN_FRAMES][16];
    uint8_t bones[2][TILE_VARIANTS][TILE_MAX * TILE_MAX];
    /* The ground's autotiles, built as their keys are first seen, and the
     * zoom level they were built for: a new level starts the cache afresh. */
    TileCache tiles;
    int tiles_level;
    /* Each visible cell's colours, plus a ring of one cell around the view,
     * since a tile's ragged edge shows its neighbours' ground. Worked out on a
     * whole redraw, and at most once a SCENE_COLOUR_MS after; the blocks grow
     * only when the view needs more cells. */
    GroundRamp *cell_ramps;
    GroundPixels *cell_pixels; /* the same cells' colours as pixels */
    /* For the same cells: what the cell's carrion looked like when drawn (0:
     * none), and whether it must be drawn again in this redraw. */
    uint32_t *cell_cover;
    bool *cell_dirty;
    int cell_ramps_capacity;
    /* Every tree picture, built at scene_init (see scene.c, tree_at), and
     * the box each one's ink fills, so drawing skips the empty part. */
    TreePicture *trees;
    TreeBox tree_box[SCENE_TREES];
    /* What is drawn back to front, each frame. Room for every body and every
     * cell of the view with its ring; grown only on a zoom or a resize. */
    SceneItem *items;
    SceneItem *items_spare; /* as many again, for merging two sorted lists */
    int items_capacity;
    int item_count; /* how many items the last frame drew */
    AnimPose *poses; /* each slot's pose this frame, for bodies drawn as items */
    /* Every cell's tile key, and the terrain they were worked out from: keys
     * change only when the terrain does, so they are not redone each redraw. */
    uint32_t *keys;
    uint8_t *keys_terrain;
    int keys_capacity;
    bool keys_valid;
    /* sin over one turn in steps, for the water's slow ripple. */
    float sine[SCENE_SINE_STEPS];
    /* The ground as last drawn, and what it was drawn from. Most frames fall
     * between two ticks and have the same ground, so they only copy it. */
    Canvas ground;
    bool ground_valid;
    bool ground_tiled; /* drawn as tiles: cells can be redrawn one by one */
    uint64_t ground_tick;
    uint64_t ground_drift_step; /* water_ms / SCENE_DRIFT_MS, not wrapped */
    uint64_t ground_colour_ms;  /* water_ms when the cells' colours were worked out */
    bool ground_colours_pending; /* a tick came since, its colours not shown yet */
    int ground_level;
    float ground_cx, ground_cy;
} Scene;

/* How the window is divided, in pixels. */
#define SCENE_TEXT_H FONT_H
#define SCENE_PANEL_W (PANEL_COLS * FONT_W)
/* Below this the window shows only a message. */
#define SCENE_MIN_COLS 20
#define SCENE_MIN_ROWS 6
#define SCENE_MIN_W (SCENE_MIN_COLS * FONT_W)
#define SCENE_MIN_H (SCENE_MIN_ROWS * FONT_H)
/* A hit shows as a scar for this many ticks, a year at the default calendar. */
#define SCENE_SCAR_TICKS 100u
/* A panel keeps at least this many text rows before the bodies get any: enough
 * for all of its lines but the family colours. */
#define SCENE_PANEL_MIN_ROWS 19
/* Rows for the enlarged view: a caption, then six of body (96 pixels). And for
 * the strip: a caption, then two lines of bodies, 48 pixels each. */
#define SCENE_ENLARGED_ROWS 7
#define SCENE_STRIP_ROWS 7
/* The enlarged view's box: the panel's width less a text column each side. */
#define SCENE_ENLARGED_W (SCENE_PANEL_W - 2 * FONT_W)
#define SCENE_ENLARGED_H ((SCENE_ENLARGED_ROWS - 1) * FONT_H)
/* The strip: SCENE_STRIP_COLUMNS bodies a line, SCENE_STRIP_LINES lines, each
 * body in a cell of its own. The panel is 160 pixels wide: four cells of 40. */
#define SCENE_STRIP_COLUMNS 4
#define SCENE_STRIP_LINES 2
#define SCENE_STRIP_CELL_W (SCENE_PANEL_W / SCENE_STRIP_COLUMNS)
#define SCENE_STRIP_CELL_H ((SCENE_STRIP_ROWS - 1) * FONT_H / SCENE_STRIP_LINES)

/* The title line on top, the help line below, the world between them and the
 * panel on the right. When the window is narrow the panel goes first. */
typedef struct {
    bool too_small;
    bool has_panel;
    int view_x, view_y, view_w, view_h;
    int panel_x, panel_y, panel_rows;
    /* Below the panel's text, family_rows rows from pixel family_y. */
    bool show_enlarged, show_strip;
    int family_y, family_rows;
    int help_y;
} Layout;

void scene_layout(Layout *l, int window_w, int window_h);

/* False when memory runs out; nothing to free then. */
bool scene_init(Scene *sc, const Sim *s);
void scene_free(Scene *sc);
/* After r: ids start again, so everything remembered is wrong. */
void scene_reset(Scene *sc);
/* Which step of the overlays' clock this time is in. While paused the water,
 * the grass and the trees are all that moves, and only once a step, so a frame
 * where this has not changed need not be drawn. */
int scene_ambient_frame(uint64_t water_ms);
/* Once per frame, after the frame's ticks. body_ms is the body clock, which
 * stops while paused: fights advance by it. */
void scene_update(Scene *sc, const Sim *s, int ticks_per_second, uint64_t body_ms);
/*
 * Draws the camera's view with its top left corner at (view_x, view_y).
 * tick_fraction (0..1) is how far the frame is towards the next tick.
 * water_ms drives the water and keeps running while paused; body_ms drives
 * the creatures' breathing, and stops while paused.
 */
void scene_draw_world(Scene *sc, Canvas *c, const Camera *cam, const Sim *s,
                      float tick_fraction, uint64_t water_ms, uint64_t body_ms,
                      int view_x, int view_y);

/* The followed creature, doing what it does in the world this moment (its
 * action, frame, blinks, facing and flash), from the detailed pictures at
 * scale 2, or 1 when its resting picture would not fit, standing on a strip of
 * the lab's ground with its shadow. A SCENE_ENLARGED_W by SCENE_ENLARGED_H box,
 * top left corner at (x, y). */
void scene_draw_followed(Scene *sc, Canvas *c, const Sim *s,
                         const Creature *followed, uint64_t now_ms, int x, int y);
/* A family's average body in the same box, the same way, resting: its species
 * from its founder's id (lineage), its role and its mean real size, as a living
 * member's is picked. now_ms is the body clock: it breathes and blinks. */
void scene_draw_sample(Scene *sc, Canvas *c, const FamilySample *sample,
                       uint32_t lineage, uint64_t now_ms, int x, int y);
/* A family's bodies over time, resting at Normal size on the lab's ground,
 * oldest first, in cells of SCENE_STRIP_CELL_W by SCENE_STRIP_CELL_H: as many
 * columns as fit in w (at most SCENE_STRIP_COLUMNS), then the next line, for
 * at most SCENE_STRIP_LINES lines. Nothing when the family has no record. */
void scene_draw_strip(Scene *sc, Canvas *c, const Families *f, uint32_t lineage,
                      int x, int y, int w);
/* What a click on the panel asks for. */
typedef enum {
    PANEL_PICK_NONE,
    PANEL_PICK_TOP_HUNTER,
    PANEL_PICK_OLDEST,
    PANEL_PICK_LARGEST_FAMILY
} PanelPick;

/* What is under window pixel (x, y) on the ordinary panel. */
PanelPick scene_panel_pick(const Layout *l, int x, int y);
/* A click at world point (wx, wy): follows the nearest creature within two
 * cells, going round the world's edges, or lets go when there is none. */
void scene_click(Inspect *in, const Sim *s, float wx, float wy);
/* Once per frame, after scene_update. Moves the camera to the followed creature
 * as drawn, between cells if it is gliding. Returns that creature, or NULL when
 * nobody is followed: if it has just died, the camera stays where it was. */
const Creature *scene_follow(const Scene *sc, Camera *cam, Inspect *in,
                             const Sim *s, float tick_fraction);
/* Corner marks around the followed creature's picture, over the world. The
 * Scene is not const here: the picture may have to be drawn into its cache. */
void scene_draw_cursor(Scene *sc, Canvas *c, const Camera *cam,
                       const Sim *s, const Creature *followed,
                       float tick_fraction, int view_x, int view_y);

/*
 * Names beside notable creatures: every living giant (one that has reached
 * giant_size, as its life story records) and the followed creature, under its
 * body, at the two closest zooms only, where a name fits beside a body. A name
 * that would reach past the view is left out rather than cut. followed may be
 * NULL.
 */
void scene_draw_names(Scene *sc, Canvas *c, const Camera *cam,
                      const Sim *s, const Creature *followed,
                      float tick_fraction, int view_x, int view_y);

/*
 * The game over screen, a box in the middle of the area with its top left corner
 * at (x, y), holding the app's lines and then the keys. It draws nothing outside
 * the area, cutting what does not fit, and it reads only the Sim's seed and
 * records: nothing about any creature, as none is left.
 */
void scene_draw_game_over(Canvas *c, const Sim *s, const char *keys, int x, int y,
                          int w, int h);

#endif
