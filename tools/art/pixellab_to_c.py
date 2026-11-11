#!/usr/bin/env python3
"""
Turns a PixelLab character's side-view frames into src/gfx/art_data.c: every
frame cut to its inked box, each pixel one of the engine's inks (skeleton.h's
INK_ values), so a family's palette recolours the art as it does the drawn
bodies.

Input: a folder per species, holding a folder per clip (idle, walk, run,
graze, bite, attack, sleep) of numbered PNGs (0.png, 1.png, ...), as PixelLab's
east-facing frames come. Canvases may differ in size (68 for the template
clips, 84 for the custom ones): they share a centre, so each is put on one
84 x 84 canvas first.

Run:  python3 tools/art/pixellab_to_c.py ART_DIR > src/gfx/art_data.c
      (needs Pillow; ART_DIR holds wolf/idle/0.png ...)
"""
import os
import sys

from PIL import Image

CANVAS = 84
# skeleton.h's inks
NONE, DEEP, SHADOW, BASE, LIGHT, HIGH, BELLY, OUTLINE, MOUTH = range(9)

# species name -> SPECIES_ value name
SPECIES = {"wolf": "SPECIES_WOLF"}
# the game's actions, the clip each is cut from, and which frames: custom clips
# start with the standing pose PixelLab keeps as frame 0, which is not part of
# a loop. (first, last) inclusive; None means all.
ACTIONS = [
    ("ACTION_REST", "idle", None),
    ("ACTION_WALK", "walk", None),
    ("ACTION_RUN", "run", None),
    ("ACTION_GRAZE", "graze", (1, None)),
    ("ACTION_BITE", "bite", (1, None)),
    ("ACTION_ATTACK", "attack", None),
    ("ACTION_SLEEP", "sleep", None),
]
# Which frames of the sleep clip are the wolf lying asleep: set per species
# after looking at the clip (the clip lies down first).
SLEEP_FRAMES = {"wolf": (5, None)}


def lum(rgb):
    return (rgb[0] + rgb[1] + rgb[2]) / 3.0


def on_canvas(path):
    im = Image.open(path).convert("RGBA")
    big = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    big.alpha_composite(im, ((CANVAS - im.width) // 2, (CANVAS - im.height) // 2))
    return big


def pixels(im):
    """Every pixel as (r, g, b, a), row by row."""
    raw = im.tobytes()
    return [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4)]


def body_level(im):
    """The most common lightness of the fur: the body's base shade."""
    counts = {}
    for (r, g, b, a) in pixels(im):
        if a >= 128 and lum((r, g, b)) >= 40:
            key = int(lum((r, g, b)))
            counts[key] = counts.get(key, 0) + 1
    return max(counts, key=counts.get)


def ink_of(px, m):
    r, g, b, a = px
    if a < 128:
        return NONE
    v = lum((r, g, b))
    if r > g + 40 and r > b + 25:
        return MOUTH
    if v < 40:
        return OUTLINE
    if v > m + 50:
        return BELLY
    if v < m - 20:
        return DEEP
    if v < m - 3:
        return SHADOW
    if v <= m + 4:
        return BASE
    if v <= m + 25:
        return LIGHT
    return HIGH


def lone_dark(inks, w, h, x, y):
    """Whether (x, y) is outline-dark with fur all round it (one pixel, or
    two side by side): an eye, not an edge."""
    def at(px, py):
        return inks[py * w + px] if 0 <= px < w and 0 <= py < h else NONE
    if at(x, y) != OUTLINE:
        return None
    group = [(x, y)] + ([(x - 1, y)] if at(x - 1, y) == OUTLINE else [])
    ring = set()
    for gx, gy in group:
        for nx, ny in ((gx - 1, gy), (gx + 1, gy), (gx, gy - 1), (gx, gy + 1)):
            if (nx, ny) not in group:
                ring.add(at(nx, ny))
    return group if ring and all(v not in (NONE, OUTLINE) for v in ring) else None


def find_eye(inks, w, h):
    """The standing frame's eye: the first lone dark pixel group in the front
    third and top half of the body. Returns its pixel, or None."""
    for y in range(h // 2):
        for x in range(w - 1, w - 1 - w // 3, -1):
            group = lone_dark(inks, w, h, x, y)
            if group:
                return min(gx for gx, _ in group), y
    return None


def nose(inks, w, h):
    """The nose tip: the frontmost inked pixel above the feet (the last four
    rows), the highest one when several are as far forward."""
    for x in range(w - 1, -1, -1):
        for y in range(h - 4):
            if inks[y * w + x] != NONE:
                return x, y
    return w - 1, 0


def place_eye(inks, w, h, offset):
    """The eye at offset from this frame's nose. Any lone dark pixel within
    two of it becomes the base shade, since the game draws its own eye there
    (and blinks it). (-1, -1) when the place is not on the fur."""
    if offset is None:
        return -1, -1
    nx, ny = nose(inks, w, h)

    def fur(x, y):
        return 0 <= x < w and 0 <= y < h and inks[y * w + x] not in (NONE, OUTLINE)
    ex, ey = nx + offset[0], ny + offset[1]
    if not fur(ex, ey) and lone_dark(inks, w, h, ex, ey) is None:
        near = [(ex + dx, ey + dy) for dy in (0, -1, 1) for dx in (0, -1, 1)
                if fur(ex + dx, ey + dy)]
        if not near:
            return -1, -1
        ex, ey = near[0]
    for y in range(ey - 2, ey + 3):
        for x in range(ex - 2, ex + 3):
            group = lone_dark(inks, w, h, x, y) if 0 <= x < w and 0 <= y < h else None
            for gx, gy in group or []:
                inks[gy * w + gx] = BASE
    return ex, ey


def frames_of(folder, span):
    names = sorted((n for n in os.listdir(folder) if n.endswith(".png")),
                   key=lambda n: int(n.split(".")[0]))
    if span is not None:
        first, last = span
        names = names[first:None if last is None else last + 1]
    return [on_canvas(os.path.join(folder, n)) for n in names]


def main():
    root = sys.argv[1]
    out = []
    w = out.append
    w("/* Generated by tools/art/pixellab_to_c.py from PixelLab frames: do not edit.")
    w(" * Each frame's inks row by row, cut to its inked box; (ax, ay) is the anchor,")
    w(" * the standing pose's middle at its lowest row, in the frame's own pixels;")
    w(" * (ex, ey) the eye, -1, -1 when none was found. */")
    w('#include "art.h"')
    w("")
    sets = []
    for name in sorted(SPECIES):
        folder = os.path.join(root, name)
        stand = on_canvas(os.path.join(folder, "idle", "0.png"))
        x0, y0, x1, y1 = stand.getbbox()
        anchor_x = (x0 + x1) / 2.0
        anchor_y = y1 - 1
        m = body_level(stand)
        sx0, sy0, sx1, sy1 = stand.getbbox()
        sw, sh = sx1 - sx0, sy1 - sy0
        sinks = [ink_of(stand.getpixel((x, y)), m) for y in range(sy0, sy1) for x in range(sx0, sx1)]
        eye = find_eye(sinks, sw, sh)
        offset = None
        if eye is not None:
            nx, ny = nose(sinks, sw, sh)
            offset = (eye[0] - nx, eye[1] - ny)
        clips = []
        for action, clip, span in ACTIONS:
            if clip == "sleep":
                span = SLEEP_FRAMES.get(name)
            path = os.path.join(folder, clip)
            if not os.path.isdir(path):
                clips.append((action, None))
                continue
            frames = frames_of(path, span)
            fnames = []
            # The game's eye for every frame of the clip or for none: an eye
            # that comes and goes would blink on its own.
            placed = []
            for im in frames:
                bx0, by0, bx1, by1 = im.getbbox()
                fw, fh = bx1 - bx0, by1 - by0
                inks = [ink_of(im.getpixel((x, y)), m)
                        for y in range(by0, by1) for x in range(bx0, bx1)]
                plain = list(inks)
                ex, ey = place_eye(inks, fw, fh, offset)
                placed.append((inks, plain, ex, ey))
            eyes = all(ex >= 0 for _, _, ex, _ in placed)
            for i, im in enumerate(frames):
                bx0, by0, bx1, by1 = im.getbbox()
                fw, fh = bx1 - bx0, by1 - by0
                inks, plain, ex, ey = placed[i]
                if not eyes:
                    inks, ex, ey = plain, -1, -1
                fname = "%s_%s_%d" % (name, clip, i)
                w("static const uint8_t %s_ink[%d] = {" % (fname, fw * fh))
                for row in range(fh):
                    w("    " + ",".join(str(v) for v in inks[row * fw:(row + 1) * fw]) + ",")
                w("};")
                # the anchor in the frame's pixels, rounded down: the middle
                # of an even width falls between two pixels
                fnames.append((fname, fw, fh, int(anchor_x - bx0), anchor_y - by0, ex, ey))
            w("static const ArtFrame %s_%s_frames[%d] = {" % (name, clip, len(fnames)))
            for fname, fw, fh, ax, ay, ex, ey in fnames:
                w("    {%d, %d, %d, %d, %d, %d, %s_ink}," % (fw, fh, ax, ay, ex, ey, fname))
            w("};")
            clips.append((action, "%s_%s" % (name, clip), len(fnames)))
        w("static const ArtClip %s_clips[ACTION_COUNT] = {" % name)
        for c in clips:
            if c[1] is None:
                w("    [%s] = {0, NULL}," % c[0])
            else:
                w("    [%s] = {%d, %s_frames}," % (c[0], c[2], c[1]))
        w("};")
        w("")
        sets.append("    {%s, %d, %d, %s_clips}," % (SPECIES[name], x1 - x0, y1 - y0, name))
    w("const ArtSet ART_SETS[] = {")
    out.extend(sets)
    w("};")
    w("const int ART_SET_COUNT = (int)(sizeof ART_SETS / sizeof ART_SETS[0]);")
    print("\n".join(out))


if __name__ == "__main__":
    main()
