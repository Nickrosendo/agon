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

# Where the trunk is, as shares of the frame's width from its back end
# (animals face east: the tail is on the left, the head on the right).
TRUNK = {"wolf": (0.30, 0.70), "otter": (0.25, 0.72)}
BAND = 4         # a dark band line every this many columns
DEPTH = 0.55     # the shell covers this share of the trunk's thickness
RAISE = 1        # pixels the shell's middle stands above the back line


def shell(inks, w, h, species):
    """A domed shell over the trunk: in each trunk column, from one pixel
    over the back (more in the middle, so the outline changes) down a share
    of the trunk's thickness. Its top row is the outline, then a light rim;
    a dark line every BAND columns splits it into plates; its lower edge is
    the outline again, so it stands apart from the fur under it. The inks
    are the fur's own shades, so the family's colour stays."""
    lo, hi = TRUNK.get(species, (0.30, 0.70))
    x0, x1 = int(w * lo), int(math.ceil(w * hi))
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
        lift = int(round(RAISE * dome + 0.4))
        depth = max(2, int(round(run * DEPTH * (0.6 + 0.4 * dome))))
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


# ---- Weapon: a horn on the forehead ----

HORN_LEN = 6     # pixels from base to tip, standing body
HEAD_RADIUS = 6  # body pixels this close to the nose count as the head
HORN_TURN = -55  # degrees from the snout's direction to the horn's (up is negative):
                 # up and forward, like a rhino's
FOREHEAD_STEPS = (2, 1, 3, 0)  # pixels in front of the eye, along the snout,
                 # tried in turn for the horn's base: straight above the eye
                 # are the ears
ANGLE_STEP = 22.5  # the horn's direction is rounded to this, so it does not jitter
HEAD_UP = 5      # the most pixels climbed from the brow to the head's top edge


def nose(inks, w, h):
    """The nose tip: the frontmost body pixel above the feet (the last four
    rows), the highest one when several are as far forward. The same rule as
    pixellab_to_c.nose."""
    for x in range(w - 1, -1, -1):
        for y in range(h - 4):
            if inks[y * w + x] != NONE:
                return x, y
    return w - 1, 0


def snout_angle(inks, w, h, nx, ny):
    """The head's direction, in degrees (0 east, 90 straight down): from the
    middle of the body pixels near the nose to the nose."""
    sx = sy = n = 0
    for y in range(max(0, ny - HEAD_RADIUS), min(h, ny + HEAD_RADIUS + 1)):
        for x in range(max(0, nx - HEAD_RADIUS), min(w, nx + 1)):
            if inks[y * w + x] != NONE and (x - nx) ** 2 + (y - ny) ** 2 <= HEAD_RADIUS ** 2:
                sx, sy, n = sx + x, sy + y, n + 1
    if n == 0:
        return 0.0
    return math.degrees(math.atan2(ny - sy / n, nx - sx / n))


def horn(inks, w, h, eye_offset, stand_angle):
    """A horn on the forehead. The eye is the nose plus eye_offset (the
    standing frame's eye seen from its nose), turned by however far this
    frame's snout has turned from the standing frame's (stand_angle), so it
    stays on the head when the head goes down to graze. The base is the last
    head pixel straight up from a few pixels in front of the eye, along the
    snout (FOREHEAD_STEPS). The horn points HORN_TURN degrees from the snout, rounded to
    ANGLE_STEP so it does not jitter, and is drawn as a line two pixels thick
    for two thirds of its length and one at the tip, dark at the base and
    light at the tip, so it reads at the game's sizes. Nothing when the
    turned eye is not on the body."""
    if eye_offset is None:
        return {}
    nx, ny = nose(inks, w, h)
    snout = snout_angle(inks, w, h, nx, ny)
    turn = math.radians(snout - stand_angle)
    ox, oy = eye_offset
    ex = nx + int(round(ox * math.cos(turn) - oy * math.sin(turn)))
    ey = ny + int(round(ox * math.sin(turn) + oy * math.cos(turn)))
    along = (math.cos(math.radians(snout)), math.sin(math.radians(snout)))
    # the first of these steps forward along the snout that is still on the
    # face; the eye's own column runs up into the ears, so it is the last try
    for step in FOREHEAD_STEPS:
        bx = ex + int(round(step * along[0]))
        by = ey + int(round(step * along[1]))
        if body(inks, w, h, bx, by):
            break
    else:
        return {}
    # up to the head's top edge, "up" as the head is tilted (at right angles
    # to the snout), at most HEAD_UP steps: straight up from a lowered head
    # climbed onto the neck
    ux, uy = along[1], -along[0]
    top = (bx, by)
    for i in range(1, HEAD_UP + 1):
        nxt = (int(round(bx + ux * i)), int(round(by + uy * i)))
        if not body(inks, w, h, *nxt):
            break
        top = nxt
    bx, by = top
    angle = round((snout + HORN_TURN) / ANGLE_STEP) * ANGLE_STEP
    dx, dy = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    out = {}
    for i in range(HORN_LEN + 1):
        x = int(round(bx + dx * i))
        y = int(round(by + dy * i))
        out[(x, y)] = HORN0 + min(4, 1 + i * 4 // HORN_LEN)
        if i < HORN_LEN * 2 // 3:
            # the thick part: one more pixel on the back side of the line
            out[(x + int(round(dy)), y - int(round(dx)))] = HORN0 + min(4, i * 4 // HORN_LEN)
    # the base sits on the head: only pixels off the body or on its top row
    return {p: ink for p, ink in out.items() if not body(inks, w, h, *p) or p[1] <= by}


def stand_angle(inks, w, h):
    """The standing frame's snout direction, in degrees: horn() turns the
    eye's offset by how far each frame's snout has turned from it."""
    nx, ny = nose(inks, w, h)
    return snout_angle(inks, w, h, nx, ny)
