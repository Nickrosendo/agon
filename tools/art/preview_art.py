#!/usr/bin/env python3
"""
Draws every frame in src/gfx/art_data.c as it is stored, one clip a row, each
on its anchor, with the game's eye place in red: to check a conversion before
building.

Run:  python3 tools/art/preview_art.py OUT.png   (needs Pillow)
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "..", "src", "gfx", "art_data.c")
# a grey stand-in for each ink: none, deep, shadow, base, light, high, belly,
# outline, mouth
COLOUR = {1: (40, 40, 50), 2: (80, 80, 95), 3: (120, 120, 135), 4: (160, 160, 175),
          5: (200, 200, 210), 6: (225, 220, 210), 7: (10, 10, 15), 8: (150, 30, 40)}
GROUND = (40, 60, 40)


def main():
    text = open(SRC).read()
    inks = {m.group(1): [int(v) for v in re.findall(r"\d+", m.group(2))]
            for m in re.finditer(r"static const uint8_t (\w+)_ink\[\d+\] = \{(.*?)\};", text, re.S)}
    rows = {}
    for f in re.findall(r"\{(\d+), (\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (\w+)_ink\}", text):
        w, h, ax, ay, ex, ey = map(int, f[:6])
        rows.setdefault(f[6].rsplit("_", 1)[0], []).append((w, h, ax, ay, ex, ey, inks[f[6]]))
    cell_w, cell_h = 70, 46
    most = max(len(v) for v in rows.values())
    sheet = Image.new("RGB", (cell_w * most, cell_h * len(rows)), GROUND)
    for r, frames in enumerate(rows.values()):
        for i, (w, h, ax, ay, ex, ey, ink) in enumerate(frames):
            im = Image.new("RGB", (w, h), GROUND)
            for y in range(h):
                for x in range(w):
                    if ink[y * w + x] in COLOUR:
                        im.putpixel((x, y), COLOUR[ink[y * w + x]])
            if ex >= 0:
                im.putpixel((ex, ey), (255, 0, 0))
            sheet.paste(im, (i * cell_w + 34 - ax, r * cell_h + 42 - ay))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(sys.argv[1])
    print("\n".join("%-12s %d frames" % (k, len(v)) for k, v in rows.items()))


if __name__ == "__main__":
    main()
