#include "lab.h"

#include <math.h>   /* floorf, sinf */
#include <stdio.h>  /* snprintf */
#include <string.h> /* strlen */

#include "font.h"
#include "gfx_hash.h"
#include "palette.h"
#include "particles.h" /* Particle, particle_position, particle_paint */
#include "pose.h"     /* pose_frame, pose_cycle_seconds, pose_eyes */
#include "species.h"

/* The mockup lab's ground (v9:673-676), in art pixels of the strip. */
#define SKY canvas_rgb(22, 30, 34)
#define GROUND_DARK canvas_rgb(44, 70, 41)
#define GROUND_LIGHT canvas_rgb(54, 90, 49)
#define GROUND_LINE canvas_rgb(65, 105, 58)
#define GROUND_ROWS LAB_GROUND_ROWS

#define MARGIN FONT_W         /* left, right and between the cells */
#define NAME_COLUMN (12 * FONT_W)  /* the role names on the left */
#define ROW_GAP 6             /* under a cell's name, before the next row */

static const char *const ROLE_TITLES[ROLE_COUNT] = {
    [ROLE_HUNTER] = "hunters",   [ROLE_SWIMMER] = "swimmers",
    [ROLE_ARMOURED] = "armoured", [ROLE_RUNNER] = "runners",
    [ROLE_WATCHER] = "watchers", [ROLE_GRAZER] = "plant-eaters",
};

/* The actions the number keys choose, key 1 first, and their names. The keys
 * are not ACTION_ values: this table maps them. On key 5 the hunters' row
 * attacks a deer instead of biting (draw_attack). */
static const int KEY_ACTIONS[LAB_KEYS] = {
    ACTION_WALK, ACTION_RUN, ACTION_REST, ACTION_GRAZE, ACTION_BITE, ACTION_SLEEP,
};
static const char *const KEY_NAMES[LAB_KEYS] = {
    "walk", "run", "rest", "graze", "bite", "sleep",
};

int lab_action_of_key(int number)
{
    return number >= 1 && number <= LAB_KEYS ? KEY_ACTIONS[number - 1] : -1;
}

int lab_species_at(int role, int column)
{
    if (role < 0 || role >= ROLE_COUNT || column < 0 || column >= LAB_COLUMNS) {
        return -1;
    }
    int seen = 0;
    for (int s = 0; s < SPECIES_COUNT; s++) {
        if (SPECIES[s].role == role) {
            if (seen == column) {
                return s;
            }
            seen++;
        }
    }
    return -1;
}

/* The mockup's role genes (v9:82-86; swimmers from the brief). */
static BodyTraits role_genes(int role)
{
    switch (role) {
    case ROLE_HUNTER:
        return (BodyTraits){0.85f, 0.5f, 0.5f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f};
    case ROLE_SWIMMER:
        return (BodyTraits){0.6f, 0.35f, 0.4f, 0.7f, 0.2f, 0.0f, 0.0f, 0.3f, 1.0f};
    case ROLE_ARMOURED:
        return (BodyTraits){0.8f, 0.2f, 0.3f, 0.8f, 0.1f, 1.0f, 0.0f, 0.3f, 0.0f};
    case ROLE_RUNNER:
        return (BodyTraits){0.25f, 1.0f, 0.5f, 0.5f, 0.2f, 0.0f, 0.0f, 1.0f, 0.0f};
    case ROLE_WATCHER:
        return (BodyTraits){0.3f, 0.4f, 1.0f, 0.6f, 0.2f, 0.0f, 0.0f, 0.35f, 0.0f};
    default: /* plant-eaters */
        return (BodyTraits){0.5f, 0.4f, 0.35f, 1.0f, 0.0f, 0.0f, 0.0f, 0.55f, 0.0f};
    }
}

/* The size the lab shows a species at: 2, or what its place in the table needs. */
static float lab_size(int species)
{
    if (species == SPECIES_HIPPO || species == SPECIES_ELEPHANT ||
        species == SPECIES_RHINO) {
        return 6.5f;
    }
    return species == SPECIES_ARMADILLO ? 1.5f : 2.0f;
}

static SpriteBody lab_body(int species)
{
    BodyTraits genes = role_genes(SPECIES[species].role);
    return sprite_body_of(&genes, species, lab_size(species), SPRITE_BASE_DETAIL);
}

/* Where the page puts things, worked out from the area. Each row's strip is as
 * tall as its tallest animal needs, so a row of otters does not pay for the
 * elephant's. */
typedef struct {
    bool full;          /* false: the area is too small for the page */
    CanvasClip area;
    int row_y[ROLE_COUNT], strip_h[ROLE_COUNT];
    int cells_x, cell_w;
    int scale;
} LabLayout;

/* The tallest picture of each role's row and the widest of all, in art pixels,
 * from the inked bounds of the pictures at this action and frame. */
static void largest_pictures(SpriteCache *sprites, int action, int frame,
                             int row_h[ROLE_COUNT], int *max_w)
{
    *max_w = 0;
    for (int r = 0; r < ROLE_COUNT; r++) {
        row_h[r] = 0;
    }
    for (int s = 0; s < SPECIES_COUNT; s++) {
        SpriteBody b = lab_body(s);
        SpritePicture p = sprite_picture(sprites, &b, action, frame);
        int role = SPECIES[s].role;
        if (p.x1 - p.x0 > *max_w) {
            *max_w = p.x1 - p.x0;
        }
        if (p.y1 - p.y0 > row_h[role]) {
            row_h[role] = p.y1 - p.y0;
        }
    }
}

/* Spare pixels a strip may take as headroom, per art pixel of scale. */
#define MAX_SPARE_PER_SCALE 8
#define MAX_SCALE 3

static LabLayout make_layout(SpriteCache *sprites, int action, int frame, int x, int y,
                             int w, int h)
{
    LabLayout l = {0}; /* full false, everything else 0 */
    l.area = (CanvasClip){x, y, w, h};
    l.cells_x = x + MARGIN + NAME_COLUMN + MARGIN;
    l.cell_w = (x + w - MARGIN - l.cells_x - (LAB_COLUMNS - 1) * MARGIN) / LAB_COLUMNS;
    int top = y + FONT_H + 4;  /* the title line, then a little air */
    int room = y + h - 4 - top;
    if (w <= 0 || h <= 0 || l.cell_w < 24 || room < ROLE_COUNT * (FONT_H + ROW_GAP + 24)) {
        return l;
    }
    int row_h[ROLE_COUNT], max_w;
    largest_pictures(sprites, action, frame, row_h, &max_w);
    /* The largest scale at which every row fits: its animal, the ground under
     * it and two pixels of air, then the caption; and the widest fits a cell. */
    for (int s = MAX_SCALE; s >= 1; s--) {
        int need = ROLE_COUNT * (FONT_H + ROW_GAP);
        for (int r = 0; r < ROLE_COUNT; r++) {
            need += (row_h[r] + GROUND_ROWS + 2) * s;
        }
        if ((need <= room && (max_w + 2) * s <= l.cell_w) || s == 1) {
            l.scale = s;
            int spare = (room - need) / ROLE_COUNT;  /* negative at 1 when cramped */
            if (spare < 0) {
                spare = 0;
            }
            if (spare > MAX_SPARE_PER_SCALE * s) {
                spare = MAX_SPARE_PER_SCALE * s;
            }
            int at = top;
            for (int r = 0; r < ROLE_COUNT; r++) {
                l.strip_h[r] = (row_h[r] + GROUND_ROWS + 2) * s + spare;
                l.row_y[r] = at;
                at += l.strip_h[r] + FONT_H + ROW_GAP;
            }
            break;
        }
    }
    l.full = true;
    return l;
}

int lab_cell_box(SpriteCache *sprites, int action, int w, int h, int role, int column,
                 int *bx, int *by, int *bw, int *bh)
{
    LabLayout l = make_layout(sprites, action, 0, 0, 0, w, h);
    if (!l.full || role < 0 || role >= ROLE_COUNT || column < 0 || column >= LAB_COLUMNS) {
        return 0;
    }
    *bx = l.cells_x + column * (l.cell_w + MARGIN);
    *by = l.row_y[role];
    *bw = l.cell_w;
    *bh = l.strip_h[role];
    return l.scale;
}

/* Text kept whole inside the area: cut to the columns that fit, and not drawn
 * at all when the row does not fit. */
static void text_in(Canvas *c, const LabLayout *l, int x, int y, const char *text,
                    uint32_t fg, uint32_t bg)
{
    if (y < l->area.y || y + FONT_H > l->area.y + l->area.h || x < l->area.x) {
        return;
    }
    int columns = (l->area.x + l->area.w - x) / FONT_W;
    char line[64];
    snprintf(line, sizeof(line), "%.*s", columns < 63 ? columns : 63, text);
    if (line[0] != '\0') {
        font_text(c, x, y, line, fg, bg);
    }
}

static uint32_t pixel_of(Colour k)
{
    return canvas_rgb(k.r, k.g, k.b);
}

static uint32_t dimmed(Colour k)
{
    return canvas_rgb((uint8_t)(k.r * 3 / 4), (uint8_t)(k.g * 3 / 4), (uint8_t)(k.b * 3 / 4));
}

/* The title and, to its right, the actions to choose between: the chosen one
 * bright, the others dim. Whole items only. */
static void draw_title(Canvas *c, const LabLayout *l, int action, uint32_t bg)
{
    uint32_t bright = pixel_of(PALETTE_TEXT), dim = dimmed(PALETTE_TEXT);
    int x = l->area.x + MARGIN;
    text_in(c, l, x, l->area.y, "species lab   b closes", bright, bg);
    x += (int)strlen("species lab   b closes") * FONT_W + 3 * FONT_W;
    for (int k = 0; k < LAB_KEYS; k++) {
        char item[24];
        snprintf(item, sizeof(item), "%d %s", k + 1, KEY_NAMES[k]);
        int width = (int)strlen(item) * FONT_W;
        if (x + width > l->area.x + l->area.w) {
            return;
        }
        text_in(c, l, x, l->area.y, item, KEY_ACTIONS[k] == action ? bright : dim, bg);
        x += width + 2 * FONT_W;
    }
}

uint32_t lab_sky(void)
{
    return SKY;
}

int lab_draw_ground(Canvas *c, int x, int y, int w, int h, int scale, int salt,
                    CanvasClip clip)
{
    int s = scale;
    int line_y = y + h - LAB_GROUND_ROWS * s;
    canvas_rect_clip(c, x, y, w, line_y - y, SKY, clip);
    canvas_rect_clip(c, x, line_y, w, s, GROUND_LINE, clip);
    for (int row = line_y + s; row < y + h; row += s) {
        int run_start = x;
        uint32_t run_colour = 0;
        for (int col = x; col < x + w; col += s) {
            /* Pairs of columns share a hash (the mockup's x >> 1). */
            int art_x = (col - x) / s, art_y = (row - line_y) / s;
            uint32_t colour = gfx_hash(art_x >> 1, art_y, 3 + salt) > 0.5f ? GROUND_DARK
                                                                          : GROUND_LIGHT;
            if (col == x) {
                run_colour = colour;
            } else if (colour != run_colour) {
                canvas_rect_clip(c, run_start, row, col - run_start, s, run_colour, clip);
                run_start = col;
                run_colour = colour;
            }
        }
        canvas_rect_clip(c, run_start, row, x + w - run_start, s, run_colour, clip);
    }
    return line_y;
}

/* One cell's strip of ground, cut by the page's area. */
static void draw_strip_ground(Canvas *c, const LabLayout *l, int role, int sx, int sy,
                              int salt)
{
    lab_draw_ground(c, sx, sy, l->cell_w, l->strip_h[role], l->scale, salt, l->area);
}

/* The eye, as the world draws it (v9:483-492): one pixel, or a larger one
 * with a glint in a big-eyed species or a big body. Facing right, or
 * mirrored inside the picture's inked width w when flip is set. */
static void draw_eye(Canvas *c, const SpritePicture *p, const SpritePalette *pal,
                     float eyes, int left, int top, int scale, bool flip, CanvasClip clip)
{
    SpriteEyePixel eye[SPRITE_EYE_PIXELS_MAX];
    int n = sprite_eye_pixels(p, eyes, eye);
    int w = p->x1 - p->x0;
    for (int k = 0; k < n; k++) {
        int col = p->eye_x + eye[k].dx - p->x0;
        if (flip) {
            col = w - 1 - col;
        }
        int ex = left + col * scale;
        int ey = top + (p->eye_y + eye[k].dy - p->y0) * scale;
        canvas_pixel(c, ex, ey, scale, pal->ink[eye[k].ink], clip);
    }
}

/* The strip of one cell, cut by the area too: a clip is a plain rectangle. */
static CanvasClip strip_clip(const LabLayout *l, int sx, int sy, int strip_h)
{
    CanvasClip strip = {sx, sy, l->cell_w, strip_h};
    int x1 = strip.x + strip.w < l->area.x + l->area.w ? strip.x + strip.w : l->area.x + l->area.w;
    int y1 = strip.y + strip.h < l->area.y + l->area.h ? strip.y + strip.h : l->area.y + l->area.h;
    strip.x = strip.x > l->area.x ? strip.x : l->area.x;
    strip.y = strip.y > l->area.y ? strip.y : l->area.y;
    strip.w = x1 - strip.x;
    strip.h = y1 - strip.y;
    return strip;
}

/* One picture with its shadow and eye, its inked box's top left corner at
 * (left, top) on the canvas, mirrored when flip is set. */
static void paint_animal(Canvas *c, const SpritePicture *p, const SpritePalette *pal,
                         float eyes, int left, int top, int s, bool flip, CanvasClip strip)
{
    int w = p->x1 - p->x0, h = p->y1 - p->y0;
    /* The ground point (ox, oy) is where the shadow goes; mirrored with the
     * body. */
    int col = p->ox - p->x0 + p->shadow_dx;
    if (flip) {
        col = w - 1 - col;
    }
    int foot_x = left + col * s;
    int foot_y = top + (p->oy - p->y0) * s;
    canvas_darken_ellipse(c, foot_x, foot_y, p->shadow_rx * s, p->shadow_ry * s, 141, strip);
    canvas_picture(c, p->ink + p->y0 * p->w + p->x0, p->w, w, h, left, top, s, flip,
                   pal->ink, strip);
    draw_eye(c, p, pal, eyes, left, top, s, flip, strip);
}

/* Where a picture's inked box goes so its ground point lands on canvas column
 * foot_x and its feet on the strip's first row of ground (line_y): the box's
 * left edge, written to *left and *top. */
static void place_by_foot(const SpritePicture *p, int foot_x, int line_y, int s, bool flip,
                          int *left, int *top)
{
    int w = p->x1 - p->x0;
    int col = p->ox - p->x0;
    if (flip) {
        col = w - 1 - col;
    }
    *left = foot_x - col * s;
    *top = line_y + 2 * s - (p->oy - p->y0 + 1) * s;
}

/* ---- Particles, worked out from the clock ----
 *
 * The lab keeps nothing between frames, so its particles are not a pool: a
 * puff is due every period_ms, and at any moment the ones still alive are the
 * last few due, each as old as the time since. Where each one is follows from
 * its age (particle_position), and its random numbers from gfx_hash of its
 * number, so a frame is the same however it is reached. */

/* The lab's leaves (v9:690): always this green. */
static const Colour LEAF = {130, 210, 100};
static const Colour Z_COLOUR = {220, 230, 255};

/* One particle, its numbers in art pixels from the ground point at canvas
 * (foot_x, foot_y), at this age. */
static void paint_particle(Canvas *c, const Particle *p, float age, int foot_x, int foot_y,
                           int s, CanvasClip clip)
{
    float x, y;
    particle_position(p, age, &x, &y);
    /* floorf(v + 0.5f) rounds as the mockup's Math.round; (int) of a negative
     * number would cut towards zero instead. */
    particle_paint(p, c, foot_x + (int)floorf(x + 0.5f) * s,
                   foot_y + (int)floorf(y + 0.5f) * s, s, clip);
}

/* Where on the drawn picture puffs start, in art pixels from its ground
 * point: its front edge (+ east) and its top (negative: up). */
typedef struct {
    float front, top;
} PuffPlace;

/* Fills *out with the puff that came k periods after the species' first.
 * A pointer to a function: paint_puffs is handed which maker to call. */
typedef void (*PuffMaker)(Particle *out, int species, uint32_t k, PuffPlace at);

/* Every puff of a species still alive at body_ms: one every period_ms, the
 * species shifting its own start so the page is not in step. */
static void paint_puffs(Canvas *c, PuffMaker make, int species, PuffPlace at,
                        uint64_t period_ms, float life, uint64_t body_ms, int foot_x,
                        int foot_y, int s, CanvasClip clip)
{
    uint64_t offset = (uint64_t)species * 97u % period_ms;
    if (body_ms < offset) {
        return; /* none due yet: unsigned subtraction would wrap */
    }
    uint64_t k = (body_ms - offset) / period_ms; /* the newest puff */
    for (;;) {
        float age = (float)(body_ms - offset - k * period_ms) / 1000.0f;
        if (age >= life) {
            break;
        }
        Particle p;
        make(&p, species, (uint32_t)k, at);
        paint_particle(c, &p, age, foot_x, foot_y, s, clip);
        if (k == 0) {
            break; /* k is unsigned: k-- past 0 would wrap to a huge number */
        }
        k--;
    }
}

/* A leaf flying forward off the grass at the grazing mouth (v9:690): from the
 * picture's front edge, low down, where the head comes down to the grass. */
static void make_leaf(Particle *out, int species, uint32_t k, PuffPlace at)
{
    *out = (Particle){.x = at.front, .y = -4.0f, .life = 0.5f, .grav = 1.0f,
                      .colour = canvas_rgb(LEAF.r, LEAF.g, LEAF.b), .kind = PARTICLE_DOT};
    out->vx = 10.0f + gfx_hash((int)k, species, 1) * 25.0f;
    out->vy = -25.0f - gfx_hash((int)k, species, 2) * 10.0f;
}

/* A Z rising over a sleeper (v9:691): just above its lying body, its own 5
 * rows clear of the picture's top. */
static void make_z(Particle *out, int species, uint32_t k, PuffPlace at)
{
    (void)species; /* every sleeper's Z is the same: (void) says "unused" */
    (void)k;
    *out = (Particle){.x = 6.0f, .y = at.top - 7.0f, .vx = 3.0f, .vy = -6.0f, .life = 1.6f,
                      .colour = canvas_rgb(Z_COLOUR.r, Z_COLOUR.g, Z_COLOUR.b),
                      .kind = PARTICLE_Z};
}

/* Blood and fur from the deer's neck at a bite (v9:689), drop j of 12 (seven
 * drops, then five tufts) after the bite numbered k, flying away from the
 * hunter, which stands to the west. The point is 7 art pixels in front of the
 * deer's feet and 13 up. */
static void make_blood(Particle *out, uint32_t fur, uint32_t k, int j)
{
    float r1 = gfx_hash((int)k, j, 1), r2 = gfx_hash((int)k, j, 2);
    float r3 = gfx_hash((int)k, j, 3);
    if (j < 7) {
        *out = (Particle){.x = -7.0f, .y = -13.0f + r1 * 3.0f, .vx = 8.0f + r2 * 30.0f,
                          .vy = -20.0f - r3 * 25.0f, .grav = 1.2f,
                          .life = 0.55f + gfx_hash((int)k, j, 4) * 0.2f,
                          .colour = canvas_rgb((uint8_t)(176.0f + r1 * 40.0f), 28, 40),
                          .kind = PARTICLE_DOT};
    } else {
        *out = (Particle){.x = -7.0f, .y = -13.0f, .vx = (r1 - 0.3f) * 30.0f,
                          .vy = -15.0f - r2 * 15.0f, .grav = 0.25f, .life = 0.8f,
                          .colour = fur, .kind = PARTICLE_DOT};
    }
}

/* The blood and fur of the hunter's last bite, if still flying: the attack
 * reaches the bite at frame IMPACT_FRAME of its cycle, so the bite's age is
 * the frames since then, plus the time into the present frame. */
#define IMPACT_FRAME 27 /* POSE_IMPACT_AT of the attack's 54 frames */
static void paint_bite(Canvas *c, int frame, uint64_t body_ms, uint32_t fur, int prey_x,
                       int foot_y, int s, CanvasClip clip)
{
    int n = SPRITE_FRAMES[ACTION_ATTACK];
    float frame_ms = 1000.0f * POSE_ATTACK_SECONDS / (float)n;
    uint64_t cycle_ms = (uint64_t)(POSE_ATTACK_SECONDS * 1000.0f + 0.5f);
    uint64_t in_cycle = body_ms % cycle_ms;
    /* pose_frame's own split of the cycle into frames (pose.c). */
    uint64_t by_time = in_cycle * (uint64_t)n / cycle_ms;
    float into_frame = (float)in_cycle - (float)by_time * frame_ms;
    int since = (frame - IMPACT_FRAME + n) % n; /* + n: % keeps a minus sign */
    float age_ms = (float)since * frame_ms + into_frame;
    if (age_ms >= 800.0f || age_ms > (float)body_ms) {
        return; /* all landed, or the bite is from before the clock started */
    }
    uint32_t k = (uint32_t)(((float)body_ms - age_ms) / (float)cycle_ms);
    for (int j = 0; j < 12; j++) {
        Particle p;
        make_blood(&p, fur, k, j);
        float age = age_ms / 1000.0f;
        if (age < p.life) {
            paint_particle(c, &p, age, prey_x, foot_y, s, clip);
        }
    }
}

/* One species standing on its strip: shadow, then the picture cut to its inked
 * bounds and centred, its feet on the first row of ground. */
static void draw_animal(SpriteCache *sprites, Canvas *c, const LabLayout *l, int species,
                        int action, uint64_t body_ms, int sx, int sy)
{
    int s = l->scale;
    int strip_h = l->strip_h[SPECIES[species].role];
    SpriteBody body = lab_body(species);
    /* Each animal at its own pace, the species number in place of an id so
     * the page is not in step. */
    uint32_t id = (uint32_t)species;
    int frame = pose_frame(body_ms, id, action, pose_cycle_seconds(&body, action));
    SpritePicture p = sprite_picture(sprites, &body, action, frame);
    bool blinks = action == ACTION_REST || action == ACTION_WALK || action == ACTION_RUN;
    float eyes = action == ACTION_SLEEP ? 0.0f : blinks ? pose_eyes(body_ms, id) : 1.0f;
    SpritePalette pal;
    sprite_palette_look(&pal, species);
    int w = p.x1 - p.x0, h = p.y1 - p.y0;
    if (w <= 0 || h <= 0) {
        return;
    }
    int line_y = sy + strip_h - GROUND_ROWS * s;
    int left = sx + (l->cell_w - w * s) / 2;
    int top = line_y + 2 * s - h * s;  /* the feet's last row is the ground's first */
    CanvasClip strip = strip_clip(l, sx, sy, strip_h);
    paint_animal(c, &p, &pal, eyes, left, top, s, false, strip);
    /* The ground point on the canvas: the column of ox, the row of oy. */
    int foot_x = left + (p.ox - p.x0) * s;
    int foot_y = top + (p.oy - p.y0) * s;
    PuffPlace at = {(float)(p.x1 - p.ox), (float)(p.y0 - p.oy)};
    if (action == ACTION_GRAZE) {
        paint_puffs(c, make_leaf, species, at, 150u, 0.5f, body_ms, foot_x, foot_y, s, strip);
    } else if (action == ACTION_SLEEP) {
        paint_puffs(c, make_z, species, at, 1200u, 1.6f, body_ms, foot_x, foot_y, s, strip);
    }
}

/* How far apart the hunter's and the deer's ground points stand, in art
 * pixels (v9:677-692: the hunter at 16, the deer at 47), and how far left of
 * the strip's middle the hunter stands, so the pair is about centred. */
#define PREY_GAP 31
#define ATTACKER_LEFT 18

/*
 * A hunter attacking a deer that faces it (v9:677-692): the hunter plays the
 * attack straight through, looping, and the deer stands at rest, flashing and
 * knocked back when the bite lands. Both are placed by their ground points, not
 * centred: a centred box would slide about as the pounce stretches it.
 */
static void draw_attack(SpriteCache *sprites, Canvas *c, const LabLayout *l, int species,
                        uint64_t body_ms, int sx, int sy)
{
    int s = l->scale;
    int strip_h = l->strip_h[SPECIES[species].role];
    int line_y = sy + strip_h - GROUND_ROWS * s;
    CanvasClip strip = strip_clip(l, sx, sy, strip_h);
    uint32_t id = (uint32_t)species;
    int attacker_x = sx + l->cell_w / 2 - ATTACKER_LEFT * s;

    SpriteBody hunter = lab_body(species);
    int frame = pose_frame(body_ms, id, ACTION_ATTACK, POSE_ATTACK_SECONDS);
    float share = (float)frame / (float)SPRITE_FRAMES[ACTION_ATTACK];
    float flash, knock, tremble;
    pose_hit(share, &flash, &knock, &tremble);

    /* The deer first, so the hunter's leap is drawn over it. */
    SpriteBody deer = lab_body(SPECIES_DEER);
    int deer_frame = pose_frame(body_ms, id, ACTION_REST, POSE_REST_SECONDS);
    SpritePicture prey = sprite_picture(sprites, &deer, ACTION_REST, deer_frame);
    SpritePalette look;
    sprite_palette_look(&look, SPECIES_DEER);
    /* Knock-back in the hunter's K units, tremble in art pixels (v9:682). */
    float t = (float)(body_ms % 60000u) / 1000.0f;
    float push = knock * sprite_k(&hunter) + tremble * sinf(t * 40.0f);
    int foot_x = attacker_x + PREY_GAP * s + (int)(push * (float)s);
    if (prey.x1 > prey.x0) {
        SpritePalette pal;
        sprite_palette_flash(&pal, &look, flash);
        int left, top;
        place_by_foot(&prey, foot_x, line_y, s, true, &left, &top);
        paint_animal(c, &prey, &pal, 1.0f, left, top, s, true, strip);
    }
    /* Asked for only after the deer is painted: a picture stays good only
     * until the next sprite_picture call, which may reuse its cache entry. */
    SpritePicture p = sprite_picture(sprites, &hunter, ACTION_ATTACK, frame);
    if (p.x1 > p.x0) {
        SpritePalette pal;
        sprite_palette_look(&pal, species);
        int left, top;
        place_by_foot(&p, attacker_x, line_y, s, false, &left, &top);
        paint_animal(c, &p, &pal, 1.0f, left, top, s, false, strip);
    }
    /* Over both: the ground point's row is the line's next (place_by_foot). */
    paint_bite(c, frame, body_ms, look.ink[INK_LIGHT], foot_x, line_y + s, s, strip);
}

void lab_draw(SpriteCache *sprites, Canvas *c, int action, uint64_t body_ms, int x, int y,
              int w, int h)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    uint32_t bg = pixel_of(PALETTE_TEXT_BACKGROUND);
    if (action < 0 || action >= ACTION_COUNT) {
        action = ACTION_REST;
    }
    /* The rows are sized by the action's first frame, so the page does not
     * jump about as the animals move. */
    LabLayout l = make_layout(sprites, action, 0, x, y, w, h);
    canvas_rect_clip(c, x, y, w, h, bg, l.area);
    if (!l.full) {
        return;
    }
    draw_title(c, &l, action, bg);
    for (int role = 0; role < ROLE_COUNT; role++) {
        int row_y = l.row_y[role];
        text_in(c, &l, x + MARGIN, row_y + (l.strip_h[role] - FONT_H) / 2, ROLE_TITLES[role],
                pixel_of(PALETTE_TEXT), bg);
        for (int col = 0; col < LAB_COLUMNS; col++) {
            int species = lab_species_at(role, col);
            if (species < 0) {
                continue;
            }
            int sx = l.cells_x + col * (l.cell_w + MARGIN);
            draw_strip_ground(c, &l, role, sx, row_y, species);
            if (action == ACTION_BITE && role == ROLE_HUNTER) {
                draw_attack(sprites, c, &l, species, body_ms, sx, row_y);
            } else {
                draw_animal(sprites, c, &l, species, action, body_ms, sx, row_y);
            }
            const char *name = SPECIES[species].name;
            int name_x = sx + (l.cell_w - (int)strlen(name) * FONT_W) / 2;
            text_in(c, &l, name_x > sx ? name_x : sx, row_y + l.strip_h[role] + 2, name,
                    dimmed(PALETTE_TEXT), bg);
        }
    }
}
