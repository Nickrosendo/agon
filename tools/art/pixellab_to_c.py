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
128 x 128 canvas first.

Run:  python3 tools/art/pixellab_to_c.py ART_DIR > src/gfx/art_data.c
      (needs Pillow; ART_DIR holds wolf/idle/0.png ...)
"""
import os
import sys

from PIL import Image

CANVAS = 128  # larger than any PixelLab canvas (68 to 88 so far): only the margin changes
# skeleton.h's inks
NONE, DEEP, SHADOW, BASE, LIGHT, HIGH, BELLY, OUTLINE, MOUTH = range(9)
HORN0 = 11  # INK_HORN0: five horn shades, dark to light, fixed colours
MANE0 = 25  # INK_MANE0: five mane shades, dark to light, from the family's colour

# species name -> its SPECIES_ value and the parts its frames already show
# (art.h's ART_COVERS_ bits): a swimmer's body is its fin, an armoured one's
# its plates (and the elephant's tusks and the rhino's horn its weapon), so
# their genes for those do not send it back to the skeleton.
SPECIES = {
    "wolf": ("SPECIES_WOLF", "0"),
    "fox": ("SPECIES_FOX", "0"),
    "goat": ("SPECIES_GOAT", "0"),
    "otter": ("SPECIES_OTTER", "ART_COVERS_SWIM"),
    "armadillo": ("SPECIES_ARMADILLO", "ART_COVERS_ARMOUR"),
    "elephant": ("SPECIES_ELEPHANT", "ART_COVERS_ARMOUR | ART_COVERS_WEAPON"),
    "lion": ("SPECIES_LION", "0"),
    "tiger": ("SPECIES_TIGER", "0"),
    "beaver": ("SPECIES_BEAVER", "ART_COVERS_SWIM"),
    "hare": ("SPECIES_HARE", "0"),
    "lemur": ("SPECIES_LEMUR", "0"),
    "fennec": ("SPECIES_FENNEC", "0"),
    "hippo": ("SPECIES_HIPPO", "ART_COVERS_SWIM"),
    "rhino": ("SPECIES_RHINO", "ART_COVERS_ARMOUR | ART_COVERS_WEAPON"),
    "gazelle": ("SPECIES_GAZELLE", "0"),
    "deer": ("SPECIES_DEER", "0"),
    "zebra": ("SPECIES_ZEBRA", "0"),
}
# the game's actions and the clip each is cut from
ACTIONS = [
    ("ACTION_REST", "idle"),
    ("ACTION_WALK", "walk"),
    ("ACTION_RUN", "run"),
    ("ACTION_GRAZE", "graze"),
    ("ACTION_BITE", "bite"),
    ("ACTION_ATTACK", "attack"),
    ("ACTION_SLEEP", "sleep"),
]
# A clip drawn from another clip's frames, when PixelLab's own came out
# broken: the armadillo's run had a blue blob and white chunks in it, the
# hippo's and the rhino's rest a skin so mottled it broke into specks (their
# rest is the eating clip's standing first frame).
CLIP_FROM = {"armadillo": {"run": "walk"}, "hippo": {"idle": "graze"},
             "rhino": {"idle": "graze"}}

# Species whose red-brown pixels are horn (drawn in the engine's fixed horn
# colours), not the inside of a mouth.
RED_IS_HORN = {"goat"}

# Species whose reddish pixels are fur whatever the fur's own colour: the
# hare's and the fennec's pink inner ears and the hippo's pink muzzle and
# belly would otherwise be drawn as the inside of a mouth.
RED_IS_FUR = {"hare", "fennec", "hippo"}

# Rows at the top of the standing pose that are only ears, skipped when
# looking for the eye: a gap in the fennec's big ear's outline looked like one.
EAR_ROWS = {"fennec": 10}

# Species the game draws no eye on, because the finder took something else
# for it: a dark spot at the rhino's mouth, a nostril on the hippo's muzzle,
# the base of the gazelle's ear, the deer's antlers, the zebra's mane (their
# real eyes touch a fold, the outline or a stripe). PixelLab's own eye stays
# in the frames; it just does not blink.
NO_EYE = {"rhino", "hippo", "gazelle", "deer", "zebra"}

# Species whose frames come mottled (the gazelle's head-butt, the deer's
# eating): a lone
# shadow or light pixel with the base shade on at least three of its four
# sides becomes base, so the texture does not turn into specks.
SMOOTH = {"gazelle", "deer"}

# Species with a mane much darker than their fur (the lion's): pixels between
# the outline and 60 below the fur's lightness are mane, drawn in the
# engine's mane inks as the skeleton's lion is, instead of the fur's deepest
# shade, which beside the outline read as a black blot.
DARK_IS_MANE = {"lion"}

# Which frames of a clip to use, (first, last) inclusive, last None for the
# end; a clip not listed uses all. Custom clips start with the standing pose
# PixelLab keeps as frame 0, which is not part of a loop; a lying-down clip
# is cut to the frames lying down. Set after looking at each clip.
FRAMES = {
    "wolf": {"graze": (1, None), "bite": (1, None), "sleep": (5, None)},
    "goat": {"bite": (1, None), "sleep": (5, None)},
    # the attack's last three frames carry a cyan swirl stuck to the head
    "otter": {"bite": (1, None), "attack": (0, 5), "sleep": (4, None)},
    # the bite's last two frames hold a box-like object on the ground
    "elephant": {"bite": (1, 2), "sleep": (6, None)},
    "armadillo": {"bite": (1, None), "sleep": (5, None)},
    "fox": {"graze": (1, None), "bite": (1, None), "sleep": (4, None)},
    "lion": {"bite": (1, None), "sleep": (6, None)},
    "tiger": {"bite": (1, None), "sleep": (6, None)},
    # the bite's frame 2 holds a white object at the mouth
    "beaver": {"bite": (3, None), "sleep": (5, None)},
    # the rest's frame 0 has a leaf at the mouth; the attack's last frame is smeared
    "hare": {"idle": (1, None), "bite": (1, None), "attack": (1, 7), "sleep": (6, None)},
    "lemur": {"bite": (1, None), "sleep": (5, None)},
    "fennec": {"graze": (1, None), "bite": (1, None), "sleep": (4, None)},
    "hippo": {"idle": (0, 0), "bite": (1, None), "sleep": (4, None)},
    "rhino": {"idle": (0, 0), "bite": (1, None), "sleep": (5, None)},
    "gazelle": {"bite": (1, None), "sleep": (5, None)},
    "deer": {"bite": (1, None), "sleep": (6, None)},
    "zebra": {"bite": (1, None), "sleep": (6, None)},
}


def lum(rgb):
    return (rgb[0] + rgb[1] + rgb[2]) / 3.0


def body_only(im):
    """The frame with everything but its largest group of touching pixels
    (corners count as touching) made transparent: a dream bubble or a loose
    dot of dust is not part of the body."""
    w, h = im.size
    alpha = im.getchannel("A").tobytes()
    seen = bytearray(w * h)
    best = []
    for start in range(w * h):
        if alpha[start] < 128 or seen[start]:
            continue
        group, todo = [], [start]
        seen[start] = 1
        while todo:
            i = todo.pop()
            group.append(i)
            x, y = i % w, i // w
            for dy in (-1, 0, 1):
                for dx in (-1, 0, 1):
                    nx, ny = x + dx, y + dy
                    j = ny * w + nx
                    if 0 <= nx < w and 0 <= ny < h and not seen[j] and alpha[j] >= 128:
                        seen[j] = 1
                        todo.append(j)
        if len(group) > len(best):
            best = group
    keep = set(best)
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    for i in keep:
        out.putpixel((i % w, i // w), im.getpixel((i % w, i // w)))
    return out


def no_green(im):
    """The frame with clearly green pixels made transparent: grass a custom
    graze drew at the mouth (the fennec's). No species has green fur."""
    out = im.copy()
    w, h = im.size
    for y in range(h):
        for x in range(w):
            r, g, b, a = im.getpixel((x, y))
            if a >= 128 and g > r + 30 and g > b + 20:
                out.putpixel((x, y), (0, 0, 0, 0))
    return out


def on_canvas(path):
    im = no_green(Image.open(path).convert("RGBA"))
    big = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    big.alpha_composite(im, ((CANVAS - im.width) // 2, (CANVAS - im.height) // 2))
    return body_only(big)


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


def reddish(r, g, b):
    return r > g + 40 and r > b + 25


def red_body(im, m):
    """Whether the fur itself is reddish (the armadillo is): then red pixels
    are fur, not a mouth or a horn."""
    near = [(r, g, b) for (r, g, b, a) in pixels(im) if a >= 128 and abs(lum((r, g, b)) - m) <= 10]
    if not near:
        return False
    return reddish(*[sum(c[i] for c in near) / len(near) for i in range(3)])


def ink_of(px, m, red_is_horn=False, red_fur=False, mane=False):
    r, g, b, a = px
    if a < 128:
        return NONE
    v = lum((r, g, b))
    if mane and 40 <= v < m - 60:
        return MANE0 + min(4, int((v - 40) * 5 / (m - 100)))
    if reddish(r, g, b) and not red_fur:
        if red_is_horn:
            return HORN0 + min(4, int(v / 40))
        return MOUTH
    if v < 40:
        return OUTLINE
    # Wide bands: PixelLab's fur is often mottled (the elephant's skin, the
    # otter's coat), and narrow ones turned that texture into speckles of
    # light and shadow. Only real shading leaves the base.
    if v > m + 50:
        return BELLY
    if v < m - 25:
        return DEEP
    if v < m - 10:
        return SHADOW
    if v <= m + 10:
        return BASE
    if v <= m + 30:
        return LIGHT
    return HIGH


def smooth(inks, w, h):
    """Lone shadow or light pixels inside base fur become base. Decided on a
    copy, so the order the pixels are visited in cannot matter."""
    before = list(inks)
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            if before[y * w + x] not in (SHADOW, LIGHT):
                continue
            around = [before[y * w + x - 1], before[y * w + x + 1],
                      before[(y - 1) * w + x], before[(y + 1) * w + x]]
            if around.count(BASE) >= 3:
                inks[y * w + x] = BASE


def one_mane(inks, w, h):
    """Keeps only the largest patch of touching mane pixels: dark shadow on a
    leg or the belly can fall in the mane's band too, and specks of mane
    there go back to the fur's deepest shade."""
    seen, best = set(), []
    for start in range(w * h):
        if start in seen or not MANE0 <= inks[start] < MANE0 + 5:
            continue
        group, todo = [], [start]
        seen.add(start)
        while todo:
            i = todo.pop()
            group.append(i)
            x, y = i % w, i // w
            for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                j = ny * w + nx
                if 0 <= nx < w and 0 <= ny < h and j not in seen and MANE0 <= inks[j] < MANE0 + 5:
                    seen.add(j)
                    todo.append(j)
        if len(group) > len(best):
            best = group
    keep = set(best)
    for i in range(w * h):
        if MANE0 <= inks[i] < MANE0 + 5 and i not in keep:
            inks[i] = DEEP


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


def find_eye(inks, w, h, skip=0):
    """The standing frame's eye: the first lone dark pixel group in the front
    third and top half of the body. Returns its pixel, or None."""
    for y in range(skip, h // 2):
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
    for name in sorted(n for n in SPECIES if os.path.isdir(os.path.join(root, n))):
        folder = os.path.join(root, name)
        # the standing pose: the first rest frame used, from whichever clip
        # the rest is drawn from
        first = FRAMES.get(name, {}).get("idle", (0, None))[0]
        rest = CLIP_FROM.get(name, {}).get("idle", "idle")
        stand = on_canvas(os.path.join(folder, rest, "%d.png" % first))
        x0, y0, x1, y1 = stand.getbbox()
        anchor_x = (x0 + x1) / 2.0
        anchor_y = y1 - 1
        m = body_level(stand)
        sx0, sy0, sx1, sy1 = stand.getbbox()
        sw, sh = sx1 - sx0, sy1 - sy0
        sinks = [ink_of(stand.getpixel((x, y)), m, name in RED_IS_HORN,
                        red_body(stand, m) or name in RED_IS_FUR,
                        name in DARK_IS_MANE)
                 for y in range(sy0, sy1) for x in range(sx0, sx1)]
        eye = None if name in NO_EYE else find_eye(sinks, sw, sh, EAR_ROWS.get(name, 0))
        offset = None
        if eye is not None:
            nx, ny = nose(sinks, sw, sh)
            offset = (eye[0] - nx, eye[1] - ny)
        clips = []
        for action, clip in ACTIONS:
            span = FRAMES.get(name, {}).get(clip)
            path = os.path.join(folder, CLIP_FROM.get(name, {}).get(clip, clip))
            if not os.path.isdir(path):
                clips.append((action, None))
                continue
            frames = frames_of(path, span)
            # The fur's shade measured on the clip itself: PixelLab's colours
            # drift a little from clip to clip, and each clip's main fur
            # colour should be the family's base all the same.
            cm = body_level(frames[0])
            horn = name in RED_IS_HORN
            red_fur = red_body(frames[0], cm) or name in RED_IS_FUR
            fnames = []
            # The game's eye for every frame of the clip or for none: an eye
            # that comes and goes would blink on its own.
            placed = []
            for im in frames:
                bx0, by0, bx1, by1 = im.getbbox()
                fw, fh = bx1 - bx0, by1 - by0
                inks = [ink_of(im.getpixel((x, y)), cm, horn, red_fur, name in DARK_IS_MANE)
                        for y in range(by0, by1) for x in range(bx0, bx1)]
                one_mane(inks, fw, fh)
                if name in SMOOTH:
                    smooth(inks, fw, fh)
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
        value, covers = SPECIES[name]
        layers = "{{NULL, NULL}, {NULL, NULL}, {NULL, NULL}}"
        sets.append("    {%s, %s, %d, %d, %s_clips, %s}," % (value, covers, x1 - x0, y1 - y0,
                                                          name, layers))
    w("const ArtSet ART_SETS[] = {")
    out.extend(sets)
    w("};")
    w("const int ART_SET_COUNT = (int)(sizeof ART_SETS / sizeof ART_SETS[0]);")
    print("\n".join(out))


if __name__ == "__main__":
    main()
