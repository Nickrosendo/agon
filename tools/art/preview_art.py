#!/usr/bin/env python3
"""
Draws every frame in src/gfx/art_data.c as it is stored, one clip a row, each
on its anchor, with the game's eye place in red: to check a conversion before
building. A painted part's layer is drawn as its own row, over its base
clip's frames.

Run:  python3 tools/art/preview_art.py OUT.png [NAME_PREFIX]   (needs Pillow)
      NAME_PREFIX keeps only rows whose name starts with it ("otter",
      "wolf_weapon" for the horn rows, each over its base frames).
"""
import os
import re
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.environ.get("ART_DATA", os.path.join(HERE, "..", "..", "src", "gfx", "art_data.c"))
# a grey stand-in for each ink: none, deep, shadow, base, light, high, belly,
# outline, mouth, the five horn shades and the five mane shades (brown)
COLOUR = {1: (40, 40, 50), 2: (80, 80, 95), 3: (120, 120, 135), 4: (160, 160, 175),
          5: (200, 200, 210), 6: (225, 220, 210), 7: (10, 10, 15), 8: (150, 30, 40),
          11: (70, 55, 40), 12: (105, 85, 60), 13: (140, 115, 85), 14: (175, 150, 115),
          15: (210, 190, 155),
          9: (30, 35, 45), 21: (95, 105, 120), 22: (130, 140, 155), 23: (165, 175, 190),
          24: (200, 210, 220),
          25: (60, 30, 20), 26: (85, 45, 28), 27: (110, 60, 35), 28: (135, 80, 45),
          29: (160, 100, 60)}
GROUND = (40, 60, 40)


def main():
    text = open(SRC).read()
    inks = {m.group(1): [int(v) for v in re.findall(r"\d+", m.group(2))]
            for m in re.finditer(r"static const uint8_t (\w+)_ink\[\d+\] = \{(.*?)\};", text, re.S)}
    rows = {}
    for f in re.findall(r"\{(\d+), (\d+), (-?\d+), (-?\d+), (-?\d+), (-?\d+), (\w+)_ink\}", text):
        w, h, ax, ay, ex, ey = map(int, f[:6])
        rows.setdefault(f[6].rsplit("_", 1)[0], []).append((w, h, ax, ay, ex, ey, inks[f[6]]))
    parts = ("_armour_", "_swim_", "_weapon_")
    # A layer row (wolf_weapon_full_walk) is drawn over its base row (wolf_walk).
    drawn = {}
    for key, frames in rows.items():
        part = next((p for p in parts if p in key), None)
        if part is None:
            drawn[key] = [[f] for f in frames]
            continue
        species, rest = key.split(part, 1)
        base = rows.get("%s_%s" % (species, rest.split("_", 1)[1]))
        if base is None:
            continue
        drawn[key] = [[base[i], frames[i]] for i in range(len(frames))]
    # filtered after the layers found their base rows, so "wolf_weapon" shows
    # the horn rows over the wolf's frames
    if len(sys.argv) > 2:
        drawn = {k: v for k, v in drawn.items() if k.startswith(sys.argv[2])}
    cell_w, cell_h = 70, 46
    most = max(len(v) for v in drawn.values())
    sheet = Image.new("RGB", (cell_w * most, cell_h * len(drawn)), GROUND)
    for r, frames in enumerate(drawn.values()):
        for i, stack in enumerate(frames):
            for (w, h, ax, ay, ex, ey, ink) in stack:
                for y in range(h):
                    for x in range(w):
                        sx, sy = i * cell_w + 34 - ax + x, r * cell_h + 42 - ay + y
                        if ink[y * w + x] in COLOUR and 0 <= sx < sheet.width and 0 <= sy < sheet.height:
                            sheet.putpixel((sx, sy), COLOUR[ink[y * w + x]])
                if ex >= 0:
                    sheet.putpixel((i * cell_w + 34 - ax + ex, r * cell_h + 42 - ay + ey),
                                   (255, 0, 0))
    sheet.resize((sheet.width * 3, sheet.height * 3), Image.NEAREST).save(sys.argv[1])
    print("\n".join("%-24s %d frames" % (k, len(v)) for k, v in drawn.items()))


if __name__ == "__main__":
    main()
