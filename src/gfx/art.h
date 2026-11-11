/*
 * art: drawn frames for a species, used in place of the skeleton's body when
 * there are some. The frames come from PixelLab (tools/art/pixellab_to_c.py
 * turns them into art_data.c), already in the engine's inks, so a family's
 * palette recolours them like any drawn body. They are one fixed body: a body
 * whose genes show a part the frames do not have (plates, a horn, a fin)
 * keeps the skeleton's picture. Pure: the same arguments always give
 * the same pixels.
 */
#ifndef AGON_ART_H
#define AGON_ART_H

#include <stdbool.h>
#include <stddef.h> /* NULL, for art_data.c */
#include <stdint.h>

#include "skeleton.h" /* SkeletonRaster, ACTION_COUNT, the INK_ values */

/* One frame: w * h inks, row by row. (ax, ay) is the anchor, the point of
 * the standing pose's middle at its lowest row, in this frame's pixels; it
 * can lie outside the frame (a crouch's box ends above it). (ex, ey) is the
 * eye's pixel, where the game draws its own eye, or -1, -1 for none. */
typedef struct {
    int16_t w, h, ax, ay, ex, ey;
    const uint8_t *ink;
} ArtFrame;

/* One action's frames, played in order; frames 0 means none drawn. */
typedef struct {
    int16_t frames;
    const ArtFrame *frame;
} ArtClip;

/* The parts a species' frames already show, as bits of ArtSet.covers: an
 * otter's frames are a swimmer's body, an armadillo's are all shell. A gene
 * for a covered part does not send the body back to the skeleton. */
enum { ART_COVERS_ARMOUR = 1u << 0, ART_COVERS_WEAPON = 1u << 1,
       ART_COVERS_SWIM = 1u << 2 };

/* A species' clips, by ACTION_ value, and how wide and tall its standing
 * pose is in art pixels: its area is matched to the skeleton's standing body,
 * so the genes still decide how big it is drawn. */
typedef struct {
    int species;
    unsigned covers;  /* ART_COVERS_ bits */
    int16_t stand_w, stand_h;
    const ArtClip *clip;  /* ACTION_COUNT of them */
} ArtSet;

/* In art_data.c, generated. */
extern const ArtSet ART_SETS[];
extern const int ART_SET_COUNT;

/* The clip for this species and action, or NULL when it has none. A body
 * whose armour, weapon or swim gene is at PART_FROM or above gets NULL too,
 * unless the species' frames cover that part: they would hide its plates,
 * horn or fin. traits are the rounded ones. */
const ArtClip *art_clip(int species, int action, const BodyTraits *traits);
/* How many grid pixels one art pixel becomes so that the art's standing pose
 * covers as much as a standing body of w by h grid pixels: the square root of
 * the two areas' ratio, so a long low drawing next to a tall drawn body is
 * neither stretched nor much smaller. 0 when the species has no art or a size
 * is not positive. */
float art_scale_for(int species, int w, int h);

/*
 * Draws frame `frame` of clip into r, whose grid (w, h, ox, oy) is already
 * set: the anchor goes at (anchor_x, anchor_y) and every art pixel is `scale`
 * grid pixels (below 1 shrinks it). Each grid pixel takes the ink most of its
 * art pixels have, or none when fewer than a third are inked; then every inked
 * fur pixel next to an empty one becomes the outline, so a shrunk body keeps one,
 * and a base pixel under the outline turns light, one over it shadow, for
 * the rim of light the skeleton's bodies have. Sets the ink, the part
 * (PART_BODY where inked), the inked box, and the eye (-1, -1 for none).
 * frame is taken modulo the clip's frames.
 */
void art_raster(SkeletonRaster *r, const ArtClip *clip, int frame, float scale,
                float anchor_x, float anchor_y);

#endif
