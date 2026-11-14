"""
Parts made from a frame's own pixels, with no PixelLab painting: a shell over
the back, a horn on the forehead. pixellab_to_c.py calls these for every
frame of a species listed in its GENERATED table and writes the results as
ordinary layers, so the game draws them like painted ones.

Both work on a frame's inks (skeleton.h's INK_ values, row by row, w * h)
and return the part's pixels as {(x, y): ink} in the same frame's pixels.
They may return pixels just outside the frame (a horn above the head, a
shell's raised edge): the caller cuts each layer to its own box.
"""
import math

# skeleton.h's inks
NONE, DEEP, SHADOW, BASE, LIGHT, HIGH, BELLY, OUTLINE = range(8)
HORN0 = 11  # five horn shades, dark to light


def body(inks, w, h, x, y):
    return 0 <= x < w and 0 <= y < h and inks[y * w + x] != NONE


def column_top(inks, w, h, x):
    """The highest body pixel in column x, or None."""
    for y in range(h):
        if inks[y * w + x] != NONE:
            return y
    return None


def column_run(inks, w, h, x, top):
    """How many body pixels follow one another down from top in column x:
    the trunk's thickness there, legs not counted once a gap opens."""
    n = 0
    while top + n < h and inks[(top + n) * w + x] != NONE:
        n += 1
    return n


# ---- Armour: a shell over the back ----

# Where the trunk is on each species' standing frame, as shares of its width
# from its back end (animals face east: the tail is on the left, the head on
# the right), set by hand after looking at each one. In other frames the
# trunk keeps its length in pixels and its distance behind the head, which
# is found in each frame (find_head): a share of each frame's own width
# slid onto the neck when the head went down or the tail swung out.
TRUNK = {
    "wolf": (0.30, 0.70), "otter": (0.25, 0.72), "fox": (0.35, 0.72),
    "fennec": (0.33, 0.62), "hare": (0.20, 0.62), "lemur": (0.25, 0.72),
    "lion": (0.22, 0.60), "tiger": (0.25, 0.70), "beaver": (0.30, 0.75),
    "hippo": (0.12, 0.60), "zebra": (0.15, 0.58), "goat": (0.12, 0.65),
    "gazelle": (0.15, 0.60), "deer": (0.12, 0.60),
}
BAND = 4         # a dark band line every this many columns
# Per size (the game's ART_SIZE_: a gene of 1 shows the full part, one from
# PART_FROM below it the medium one): the share of the trunk's thickness the
# shell covers, the pixels its middle stands above the back line, the share
# of the trunk's length it covers (centred), and the share of the horn's
# length.
SIZES = {
    "full": {"depth": 0.55, "raise": 1, "along": 1.0, "horn": 1.0},
    "medium": {"depth": 0.38, "raise": 0, "along": 0.75, "horn": 0.6},
}


def trunk_place(species, stand_w, template):
    """The trunk from the standing frame: (how far its front end is behind
    the head's middle, its length), in pixels."""
    lo, hi = TRUNK.get(species, (0.30, 0.70))
    x0, x1 = int(stand_w * lo), int(math.ceil(stand_w * hi))
    return template[0] - x1, x1 - x0


def shell(inks, w, h, trunk, head_x, size="full"):
    """A domed shell over the trunk, which ends trunk's distance behind the
    head's middle (head_x, this frame's) and runs back its length (a medium
    shell over the middle part of it): in each trunk column, from one pixel
    over the back (more in the middle, so the outline changes; none for a
    medium shell) down a share of the trunk's thickness. Its top row is the outline, then a light rim;
    a dark line every BAND columns splits it into plates; its lower edge is
    the outline again, so it stands apart from the fur under it. The inks
    are the fur's own shades, so the family's colour stays."""
    sz = SIZES[size]
    gap, length = trunk
    cut = int(round(length * (1.0 - sz["along"]) / 2))
    x1 = min(w, head_x - gap - cut)
    x0 = max(0, head_x - gap - length + cut)
    out = {}
    span = max(1, x1 - x0 - 1)
    for x in range(x0, x1):
        top = column_top(inks, w, h, x)
        if top is None:
            continue
        run = column_run(inks, w, h, x, top)
        # a dome: tallest in the middle of the trunk, nothing at its ends
        t = (x - x0) / span
        dome = math.sin(math.pi * t)
        lift = int(round(sz["raise"] * dome + 0.4)) if sz["raise"] else 0
        depth = max(2, int(round(run * sz["depth"] * (0.6 + 0.4 * dome))))
        first, last = top - lift, top + depth - 1
        band = (x - x0) % BAND == 0 and 0 < x - x0 < x1 - x0 - 1
        for y in range(first, last + 1):
            if y == first or y == last:
                ink = OUTLINE
            elif band:
                ink = DEEP
            elif y == first + 1:
                ink = LIGHT
            else:
                ink = SHADOW
            out[(x, y)] = ink
    # the ends: an outline column closes the shell at both sides
    for x in (x0 - 1, x1):
        top = column_top(inks, w, h, x) if 0 <= x < w else None
        if top is not None:
            out[(x, top)] = OUTLINE
    return out


# ---- Weapon: a horn on the forehead (its shape; where it goes is below) ----

HORN_LEN = 6     # pixels from base to tip, standing body
HORN_ANGLE = -45  # the horn's direction on a standing body, in degrees (0 east,
                 # -90 straight up): up and forward, like a rhino's
ANGLE_STEP = 22.5  # the horn's direction is rounded to this, so it does not jitter


def nose(inks, w, h):
    """The nose tip: the frontmost body pixel above the feet (the last four
    rows), the highest one when several are as far forward. The same rule as
    pixellab_to_c.nose."""
    for x in range(w - 1, -1, -1):
        for y in range(h - 4):
            if inks[y * w + x] != NONE:
                return x, y
    return w - 1, 0


# ---- Finding the head in any frame ----

MATCH_R = 6      # the head's picture: the standing frame's pixels this close to
                 # the head's middle, body and empty alike (the outline matters)
REFINE = 8       # coarse places looked at closely
FRONT_PULL = 2.0  # points lost per pixel the head's middle strays from its
                  # standing distance behind the front edge
FRONT_FREE = 3.0  # pixels it may stray for free
TURNS = range(-45, 106, 15)  # head turns tried, in degrees (positive is down,
                 # as y grows downwards): from raised to grazing


def shade(ink):
    """An ink's kind for matching: 0 empty, 1 dark, 2 middle, 3 light, 4
    anything else (mouth, horn, mane). PixelLab's head keeps its light muzzle
    and dark eye and outline from frame to frame, so these say more than the
    outline alone."""
    if ink == NONE:
        return 0
    if ink in (DEEP, OUTLINE):
        return 1
    if ink in (SHADOW, BASE):
        return 2
    if ink in (LIGHT, HIGH, BELLY):
        return 3
    return 4


def head_template(inks, w, h, place):
    """The standing frame's head: its middle, halfway between the nose and
    the horn's base, every pixel within MATCH_R of it as (dx, dy, kind), and
    how far the middle is behind the frame's front edge."""
    nx, ny = nose(inks, w, h)
    if place is None:
        cx, cy = nx - 3, ny
    else:
        cx, cy = (nx + place[0]) // 2, (ny + place[1]) // 2

    def kind(x, y):
        return shade(inks[y * w + x]) if 0 <= x < w and 0 <= y < h else 0
    pts = [(dx, dy, kind(cx + dx, cy + dy))
           for dy in range(-MATCH_R, MATCH_R + 1) for dx in range(-MATCH_R, MATCH_R + 1)
           if dx * dx + dy * dy <= MATCH_R * MATCH_R]
    return cx, cy, pts, (w - 1) - cx


def turned(pts, degrees):
    """The template's offsets turned by degrees, rounded to whole pixels."""
    a = math.radians(degrees)
    c, s = math.cos(a), math.sin(a)
    return [(int(round(dx * c - dy * s)), int(round(dx * s + dy * c)), k) for dx, dy, k in pts]


def find_head(template, inks, w, h):
    """Where the standing frame's head is in this frame: (turn in degrees, x,
    y of its middle). Each template pixel scores 1 where the frame has the
    same kind there (empty on empty too), -1 where one is body and the other
    empty, 0 for two different body kinds; the place and turn with the best
    sum, less a cost for straying from the front (FRONT_PULL), win. Only the front of the frame is searched, two pixels at a time
    first, then one pixel around the REFINE best places and seven degrees
    either side of their turns."""
    _, _, pts, reach = template
    kinds = [shade(v) for v in inks]

    def score(offsets, x, y):
        # The middle should be about as far behind the frame's front edge as
        # on the standing frame: a back's top edge looks like a head's
        # outline, but it is far from the front. Only across: a lowered head
        # is lower, and its front edge may be an ear, so a few pixels either
        # way cost nothing.
        off = abs((w - 1 - x) - reach)
        sc = -FRONT_PULL * max(0.0, off - FRONT_FREE)
        for dx, dy, k in offsets:
            px, py = x + dx, y + dy
            f = kinds[py * w + px] if 0 <= px < w and 0 <= py < h else 0
            if f == k:
                sc += 1
            elif f == 0 or k == 0:
                sc -= 1
        return sc

    coarse = []
    x_from = max(0, w - 4 * MATCH_R)
    for turn in TURNS:
        offsets = turned(pts, turn)
        for y in range(0, h, 2):
            for x in range(x_from, w, 2):
                coarse.append((score(offsets, x, y), turn, x, y))
    # The best few coarse places are each looked at closely: the true place
    # can fall between two coarse ones and score less than a wrong one there.
    coarse.sort(reverse=True)
    best = coarse[0]
    for _, t0, x0, y0 in coarse[:REFINE]:
        for turn in (t0 - 7, t0, t0 + 7):
            offsets = turned(pts, turn)
            for y in range(y0 - 1, y0 + 2):
                for x in range(x0 - 1, x0 + 2):
                    sc = score(offsets, x, y)
                    if sc > best[0]:
                        best = (sc, turn, x, y)
    return best[1], best[2], best[3]


# ---- Weapon: a horn on the forehead ----

# Where the horn stands on each species' standing frame, set by hand after
# looking at each one: (x, y) of its base from the nose tip (nose()), and
# optionally its direction in degrees (0 east, -90 straight up) and its
# length; without them, HORN_ANGLE and HORN_LEN. Goat, gazelle and deer
# already have horns:
# theirs is drawn along their own and longer, so the old one sits inside the
# new (layers cannot erase). The game's eye is no help here: it is missing
# for most species. Rhino and elephant are not listed: their frames are
# their weapon. The deer's frames are its weapon too since 2026-11-13 (a
# straight horn along branched antlers read as a stick laid over them), so
# pixellab_to_c draws no horn on it; its entry stays because the head is
# found from the point between the nose and the horn's base, and the shell
# sits behind the head.
HORN = {
    "wolf": (-5, -4), "otter": (-5, -3), "fox": (-3, -3), "fennec": (-3, -3),
    "hare": (-4, -3), "lemur": (-3, -4), "lion": (-3, -4), "tiger": (-5, -5),
    "beaver": (-4, -4), "armadillo": (-7, -7), "hippo": (-8, -5),
    "zebra": (-5, -6),
    "goat": (-6, -7, -120, 9), "gazelle": (-7, -6, -100, 10),
    "deer": (-8, -7, -95, 10),
}
CLIMB = 4        # the most pixels a moved base climbs to the head's top edge
HORN_FOLLOWS = 0.5  # the share of the head's turn the horn's direction takes


def horn_place(inks, w, h, species):
    """The horn on the standing frame: (base x, base y, direction, length),
    or None for a species with no HORN entry."""
    if species not in HORN:
        return None
    entry = HORN[species]
    nx, ny = nose(inks, w, h)
    if len(entry) == 4:
        angle, length = entry[2], entry[3]
    else:
        angle, length = HORN_ANGLE, HORN_LEN
    return nx + entry[0], ny + entry[1], angle, length


def horn(inks, w, h, place, template, head, size="full"):
    """A horn on the forehead of this frame. place is horn_place's answer on
    the standing frame, template its head_template and head where find_head
    found the head in this frame: the base is moved and turned with the
    head, so the horn stays on it when it goes down to graze, and the
    direction turns HORN_FOLLOWS as far. The base then climbs straight up to
    the head's top edge (at most CLIMB steps), or steps down onto it when it
    landed just off it. The direction is rounded to ANGLE_STEP so it does not
    jitter. Drawn as a line two pixels thick for two thirds of its length
    (the size's share of the standing length, at least 3) and one at the
    tip, dark at the base and light at the tip, so it reads at the game's
    sizes."""
    if place is None:
        return {}
    px, py, pangle, length = place
    length = max(3, int(round(length * SIZES[size]["horn"])))
    cx, cy, _, _ = template
    turn, hx, hy = head
    a = math.radians(turn)
    dx0, dy0 = px - cx, py - cy
    bx = int(round(hx + dx0 * math.cos(a) - dy0 * math.sin(a)))
    by = int(round(hy + dx0 * math.sin(a) + dy0 * math.cos(a)))
    # the horn turns half as far as the head: turned all the way, a grazing
    # head's horn points straight ahead and reads as a beak
    angle = round((pangle + turn * HORN_FOLLOWS) / ANGLE_STEP) * ANGLE_STEP
    # the base climbs straight up to the head's top edge
    ux, uy = 0.0, -1.0
    if not body(inks, w, h, bx, by):
        for i in range(1, 4):
            q = (int(round(bx - ux * i)), int(round(by - uy * i)))
            if body(inks, w, h, *q):
                bx, by = q
                break
        else:
            return {}
    top = (bx, by)
    for i in range(1, CLIMB + 1):
        q = (int(round(bx + ux * i)), int(round(by + uy * i)))
        if not body(inks, w, h, *q):
            break
        top = q
    bx, by = top
    dx, dy = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    out = {}
    for i in range(length + 1):
        x = int(round(bx + dx * i))
        y = int(round(by + dy * i))
        out[(x, y)] = HORN0 + min(4, 1 + i * 4 // length)
        if i < length * 2 // 3:
            # the thick part: one more pixel on the back side of the line
            out[(x + int(round(dy)), y - int(round(dx)))] = HORN0 + min(4, i * 4 // length)
    # the base sits on the head: only pixels off the body or on its top row
    return {p: ink for p, ink in out.items() if not body(inks, w, h, *p) or p[1] <= by}
