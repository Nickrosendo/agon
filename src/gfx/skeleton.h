/*
 * skeleton: one pose of one body into a picture of parts and inks. A port of
 * the mockup's rasterize() and the outline of its drawCreature()
 * (attack-v9.html, lines 238-441 and 462-467). The body is a list of shapes
 * (ovals, tapered tubes, a mouth triangle), each painted over the ones before
 * it; every pixel gets the part it belongs to and an ink, a slot that a
 * palette turns into a colour later (task 8). Pure: the same body and pose
 * always give the same pixels, and no random numbers are used.
 */
#ifndef AGON_SKELETON_H
#define AGON_SKELETON_H

#include <stdbool.h>
#include <stdint.h>

#include "species.h" /* Anatomy */

/* Two grids. The small one holds every world picture (Normal and Far): its
 * size and ground point are this plan's own, v9's proportions shrunk to fit a
 * three-tile body (plan's call). The large one holds the detailed pictures of
 * the lab and the panel (task 8): it is v9's own grid, 120 by 90 with the ground
 * point at (54, 82) (v9:129). */
#define SKELETON_W 64
#define SKELETON_H 56
#define SKELETON_OX 26
#define SKELETON_OY 50
#define SKELETON_DETAIL_W 120
#define SKELETON_DETAIL_H 90
#define SKELETON_DETAIL_OX 54
#define SKELETON_DETAIL_OY 82
#define SPRITE_MAX_W 48  /* widest standing body at Normal: three tiles (design 5.4) */
#define SPRITE_MAX_H 40
#define SKELETON_SHAPES_MAX 64  /* room for the features of task 7 too */
/* What a body is doing. Here, not in sprite.h, so that skeleton.h needs nothing
 * from sprite.h, which includes it. */
enum { ACTION_REST, ACTION_WALK, ACTION_RUN, ACTION_GRAZE, ACTION_BITE,
       ACTION_ATTACK, ACTION_SLEEP, ACTION_COUNT };
enum { PART_NONE, PART_FAR, PART_TAIL, PART_BODY, PART_SHELL, PART_NECK, PART_MANE,
       PART_EAR_BACK, PART_MOUTH, PART_JAW, PART_HEAD, PART_HORN, PART_EAR,
       PART_TRUNK, PART_LEG };                        /* v9:130, in that order */
/* INK_DEEP to INK_HIGH are the five shades, dark to light; INK_NONE is an
 * empty pixel. */
enum { INK_NONE, INK_DEEP, INK_SHADOW, INK_BASE, INK_LIGHT, INK_HIGH, INK_BELLY,
       INK_OUTLINE, INK_MOUTH, INK_SHELL_BAND, INK_STRIPE,
       INK_HORN0,  /* ..INK_HORN0 + 4 */  INK_IVORY0 = INK_HORN0 + 5,
       INK_SHELL1 = INK_IVORY0 + 5, /* ..+3 */ INK_MANE0 = INK_SHELL1 + 4,
       INK_NOSE = INK_MANE0 + 5, INK_EYE, INK_EYE_WHITE, INK_TEETH, INK_COUNT };
/* Pose values in units of K (the mockup's numbers without "* K"), angles in
 * radians, the rest shares 0..1. (plan's call: so a pose needs no lengths.)
 * stride is a share of the body's own stride (Anatomy.stride). */
typedef struct {
    int action;  /* an ACTION_ value; ACTION_REST until task 11 */
    float phase, bob, neck, head_dx, head_dy, jaw, fold, eyes, breath, stretch,
          tail, ear, body_dx, stride, lift, brace, trunk, flap, crouch, leap, swipe;
    bool stepping;
} SpritePose;
void skeleton_pose_standing(SpritePose *p);  /* all 0, eyes 1, stride 1, lift 1.1 */
/* Sized for the large grid; a small-grid raster uses the first w * h of each.
 * Pixel (x, y) of the grid used is element y * w + x. */
typedef struct {
    int w, h, ox, oy;                         /* the grid used */
    uint8_t part[SKELETON_DETAIL_W * SKELETON_DETAIL_H];
    uint8_t ink[SKELETON_DETAIL_W * SKELETON_DETAIL_H];
    float hip[4][2], knee[4][2], foot[4][2];  /* in grid pixels; legs 0, 1 far, 2, 3 near; hind first */
    int eye_x, eye_y, x0, y0, x1, y1;         /* x1, y1 one past the last ink; eye -1, -1 when marks is false */
    int shapes;                               /* how many shapes were built */
    bool eye_big;
    float shadow_rx, shadow_ry, body_dx;
} SkeletonRaster;
/* marks false: the far zoom's silhouette (no marks, eyes, nose or teeth).
 * large: the large grid. */
void skeleton_raster(SkeletonRaster *out, const Anatomy *a, const SpritePose *p,
                     bool marks, bool large);
float skeleton_k_cap(int species);  /* the largest K whose widest standing body fits SPRITE_MAX_W by SPRITE_MAX_H */

#endif
