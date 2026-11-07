#include "lab.h"

#include <stdio.h>  /* snprintf */
#include <string.h> /* strlen */

#include "font.h"
#include "gfx_hash.h"
#include "palette.h"
#include "species.h"

/* The mockup lab's ground (v9:673-676), in art pixels of the strip. */
#define SKY canvas_rgb(22, 30, 34)
#define GROUND_DARK canvas_rgb(44, 70, 41)
#define GROUND_LIGHT canvas_rgb(54, 90, 49)
#define GROUND_LINE canvas_rgb(65, 105, 58)
#define GROUND_ROWS 7  /* the line and the six rows of ground under it */

#define MARGIN FONT_W         /* left, right and between the cells */
#define NAME_COLUMN (12 * FONT_W)  /* the role names on the left */
#define ROW_GAP 6             /* under a cell's name, before the next row */
#define LAB_FRAMES_PER_SECOND 30   /* animation is sampled at the window's rate */

static const char *const ROLE_TITLES[ROLE_COUNT] = {
    [ROLE_HUNTER] = "hunters",   [ROLE_SWIMMER] = "swimmers",
    [ROLE_ARMOURED] = "armoured", [ROLE_RUNNER] = "runners",
    [ROLE_WATCHER] = "watchers", [ROLE_GRAZER] = "plant-eaters",
};

/* In ACTION_ order, as the selector shows them. */
static const char *const ACTION_NAMES[ACTION_COUNT] = {
    "rest", "walk", "run", "graze", "bite", "attack", "sleep",
};

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
    for (int a = 0; a < ACTION_COUNT; a++) {
        char item[24];
        snprintf(item, sizeof(item), "%d %s", a + 1, ACTION_NAMES[a]);
        int width = (int)strlen(item) * FONT_W;
        if (x + width > l->area.x + l->area.w) {
            return;
        }
        text_in(c, l, x, l->area.y, item, a == action ? bright : dim, bg);
        x += width + 2 * FONT_W;
    }
}

/* A strip of the mockup's lab ground: sky above, a line, then ground of two
 * greens by hash, in art pixels of `scale` screen pixels. salt keeps strips
 * from repeating one another. */
static void draw_strip_ground(Canvas *c, const LabLayout *l, int role, int sx, int sy,
                              int salt)
{
    int s = l->scale;
    int strip_h = l->strip_h[role];
    int line_y = sy + strip_h - GROUND_ROWS * s;
    canvas_rect_clip(c, sx, sy, l->cell_w, line_y - sy, SKY, l->area);
    canvas_rect_clip(c, sx, line_y, l->cell_w, s, GROUND_LINE, l->area);
    for (int y = line_y + s; y < sy + strip_h; y += s) {
        int run_start = sx;
        uint32_t run_colour = 0;
        for (int x = sx; x < sx + l->cell_w; x += s) {
            /* Pairs of columns share a hash (the mockup's x >> 1). */
            int art_x = (x - sx) / s, art_y = (y - line_y) / s;
            uint32_t colour = gfx_hash(art_x >> 1, art_y, 3 + salt) > 0.5f ? GROUND_DARK
                                                                          : GROUND_LIGHT;
            if (x == sx) {
                run_colour = colour;
            } else if (colour != run_colour) {
                canvas_rect_clip(c, run_start, y, x - run_start, s, run_colour, l->area);
                run_start = x;
                run_colour = colour;
            }
        }
        canvas_rect_clip(c, run_start, y, sx + l->cell_w - run_start, s, run_colour,
                         l->area);
    }
}

/* The open eye, as the world draws it (v9:483-492): one pixel, or a larger one
 * with a glint in a big-eyed species or a big body. Facing right. */
static void draw_eye(Canvas *c, const SpritePicture *p, const SpritePalette *pal,
                     int left, int top, int scale, CanvasClip clip)
{
    if (p->eye_x < 0 || p->eye_y < 0) {
        return;
    }
    uint32_t dark = pal->ink[INK_EYE], white = pal->ink[INK_EYE_WHITE];
    int ex = left + (p->eye_x - p->x0) * scale, ey = top + (p->eye_y - p->y0) * scale;
    if (p->eye_big && p->eye_detail) {
        canvas_rect_clip(c, ex - scale, ey - scale, 3 * scale, 3 * scale, dark, clip);
        canvas_pixel(c, ex, ey - scale, scale, white, clip);
    } else if (p->eye_big || p->eye_detail) {
        canvas_rect_clip(c, ex, ey, 2 * scale, scale, dark, clip);
        canvas_pixel(c, ex + scale, ey - scale, scale, dark, clip);
        canvas_pixel(c, ex, ey - scale, scale, white, clip);
    } else {
        canvas_pixel(c, ex, ey, scale, dark, clip);
    }
}

/* One species standing on its strip: shadow, then the picture cut to its inked
 * bounds and centred, its feet on the first row of ground. */
static void draw_animal(SpriteCache *sprites, Canvas *c, const LabLayout *l, int species,
                        int action, int frame, int sx, int sy)
{
    int s = l->scale;
    int strip_h = l->strip_h[SPECIES[species].role];
    SpriteBody body = lab_body(species);
    SpritePicture p = sprite_picture(sprites, &body, action, frame);
    SpritePalette pal;
    sprite_palette_look(&pal, species);
    int w = p.x1 - p.x0, h = p.y1 - p.y0;
    if (w <= 0 || h <= 0) {
        return;
    }
    int line_y = sy + strip_h - GROUND_ROWS * s;
    int left = sx + (l->cell_w - w * s) / 2;
    int top = line_y + 2 * s - h * s;  /* the feet's last row is the ground's first */
    /* The ground point (ox, oy) is where the shadow goes. */
    int foot_x = left + (p.ox - p.x0) * s + p.shadow_dx * s;
    int foot_y = top + (p.oy - p.y0) * s;
    CanvasClip strip = {sx, sy, l->cell_w, strip_h};
    /* The strip, cut by the area too: a clip is a plain rectangle. */
    int x1 = strip.x + strip.w < l->area.x + l->area.w ? strip.x + strip.w : l->area.x + l->area.w;
    int y1 = strip.y + strip.h < l->area.y + l->area.h ? strip.y + strip.h : l->area.y + l->area.h;
    strip.x = strip.x > l->area.x ? strip.x : l->area.x;
    strip.y = strip.y > l->area.y ? strip.y : l->area.y;
    strip.w = x1 - strip.x;
    strip.h = y1 - strip.y;
    canvas_darken_ellipse(c, foot_x, foot_y, p.shadow_rx * s, p.shadow_ry * s, 141, strip);
    canvas_picture(c, p.ink + p.y0 * p.w + p.x0, p.w, w, h, left, top, s, false, pal.ink,
                   strip);
    draw_eye(c, &p, &pal, left, top, s, strip);
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
    int frame = (int)(body_ms * LAB_FRAMES_PER_SECOND / 1000u);
    LabLayout l = make_layout(sprites, action, frame, x, y, w, h);
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
            draw_animal(sprites, c, &l, species, action, frame, sx, row_y);
            const char *name = SPECIES[species].name;
            int name_x = sx + (l.cell_w - (int)strlen(name) * FONT_W) / 2;
            text_in(c, &l, name_x > sx ? name_x : sx, row_y + l.strip_h[role] + 2, name,
                    dimmed(PALETTE_TEXT), bg);
        }
    }
}
