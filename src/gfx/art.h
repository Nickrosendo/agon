/*
 * art: drawn frames for a species, used in place of the skeleton's body when
 * there are some. The frames come from PixelLab (tools/art/pixellab_to_c.py
 * turns them into art_data.c), already in the engine's inks, so a family's
 * palette recolours them like any drawn body. Every body of a species uses
 * them, whatever its genes; plates, a horn or a fin the frames do not show
 * are painted over them as layers. Pure: the same arguments always give
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
 * otter's frames are a swimmer's body, an armadillo's are all shell. A
 * covered part gets no layer painted over the frames. */
enum { ART_COVERS_ARMOUR = 1u << 0, ART_COVERS_WEAPON = 1u << 1,
       ART_COVERS_SWIM = 1u << 2 };

/* The parts painted over the frames as layers, in the order they are drawn:
 * a horn goes on last so plates never cover it. No swim layer is painted: a
 * swimming body keeps its species' own body (Nicolas, 2026-11-13), since the
 * gene only makes shallows cheap and land dear. The slot stays so the order
 * and the data layout need no change. */
enum { ART_PART_ARMOUR, ART_PART_SWIM, ART_PART_WEAPON, ART_PART_COUNT };
/* A part's two sizes: the game shows a part from a rounded gene of 2/3
 * (medium) and draws it bigger at 1 (full). */
enum { ART_SIZE_MEDIUM, ART_SIZE_FULL, ART_SIZE_COUNT };

/* A species' clips, by ACTION_ value, and how wide and tall its standing
 * pose is in art pixels: its area is matched to the skeleton's standing body,
 * so the genes still decide how big it is drawn.
 * layer[part][size] is that part's layer, ACTION_COUNT clips like clip, or
 * NULL when none was painted. A layer clip has its base clip's frame count,
 * frame N over frame N; each layer frame holds only the part's pixels, cut
 * to their own box, with the base frame's anchor given in the layer's own
 * pixels and no eye. */
typedef struct {
    int species;
    unsigned covers;  /* ART_COVERS_ bits */
    int16_t stand_w, stand_h;
    const ArtClip *clip;  /* ACTION_COUNT of them */
    const ArtClip *layer[ART_PART_COUNT][ART_SIZE_COUNT];
} ArtSet;

/* In art_data.c, generated. */
extern const ArtSet ART_SETS[];
extern const int ART_SET_COUNT;

/* The species' set of frames, or NULL when it has none. */
const ArtSet *art_set(int species);
/* The clip for this species and action, or NULL when it has none. traits
 * (the rounded ones) do not change the answer now: every body is drawn from
 * its species' frames. They stay in the signature for the parts' layers. */
const ArtClip *art_clip(int species, int action, const BodyTraits *traits);
/* The layers this body shows for this action, one slot per ART_PART_ value
 * (NULL where it shows none); returns how many slots are filled. A part shows
 * when its rounded gene is at PART_FROM or above, the species' frames do not
 * cover it, and a layer was painted: the full one at a gene of 1, the medium
 * one below, or the full one when no medium layer was painted yet. */
int art_layers(int species, int action, const BodyTraits *traits,
               const ArtClip *out[ART_PART_COUNT]);
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
 * the rim of light the skeleton's bodies have.
 * Before the outline, each layer in layers (ART_PART_COUNT slots, NULL ones
 * skipped; layers itself may be NULL) is drawn over the frame in slot order:
 * a grid pixel takes the layer's ink when any of its samples is a fin or horn
 * pixel, so a part one art pixel thin survives a shrink, or when a third are
 * armour pixels, the frame's own rule.
 * Sets the ink, the part (PART_BODY where inked, PART_SHELL under armour,
 * PART_HORN under a horn), the inked box, and the eye (-1, -1 for none).
 * frame is taken modulo the clip's frames.
 */
void art_raster(SkeletonRaster *r, const ArtClip *clip, const ArtClip *const *layers,
                int frame, float scale, float anchor_x, float anchor_y);

#endif
