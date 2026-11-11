#!/usr/bin/env python3
"""
Downloads a PixelLab character's east-facing frames into
tools/art/pixellab/<species>/<clip>/N.png, the layout pixellab_to_c.py reads.

The character's ZIP holds <state>/animations/<name>/east/frame_NNN.png. Clip
names: the templates' "walking", "running" and "idle" become walk, run and
idle; any other animation is named after its clip when made (walk, run, idle,
graze, bite, attack, sleep: animate_character's animation_name). Anything
else in the ZIP is left out.

Run:  python3 tools/art/fetch_pixellab.py CHARACTER_ID SPECIES
"""
import io
import os
import re
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
CLIPS = {"walking": "walk", "running": "run", "walk": "walk", "run": "run",
         "idle": "idle", "graze": "graze", "bite": "bite", "attack": "attack",
         "sleep": "sleep"}
URL = "https://api.pixellab.ai/mcp/characters/%s/download"


def main():
    character, species = sys.argv[1], sys.argv[2]
    if not re.fullmatch(r"[0-9a-f-]{36}", character) or not re.fullmatch(r"[a-z]+", species):
        sys.exit("usage: fetch_pixellab.py CHARACTER_ID SPECIES")
    with urllib.request.urlopen(URL % character) as reply:
        data = zipfile.ZipFile(io.BytesIO(reply.read()))
    found = {}
    for name in data.namelist():
        m = re.fullmatch(r"[^/]+/animations/([^/]+)/east/frame_(\d+)\.png", name)
        if m and m.group(1) in CLIPS:
            found.setdefault(CLIPS[m.group(1)], []).append((int(m.group(2)), name))
    for clip, frames in sorted(found.items()):
        folder = os.path.join(HERE, "pixellab", species, clip)
        os.makedirs(folder, exist_ok=True)
        for old in os.listdir(folder):
            os.remove(os.path.join(folder, old))
        for number, name in frames:
            with open(os.path.join(folder, "%d.png" % number), "wb") as out:
                out.write(data.read(name))
        print("%-7s %d frames" % (clip, len(frames)))


if __name__ == "__main__":
    main()
