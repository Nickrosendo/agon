#!/usr/bin/env python3
"""
Gets a species' frames ready for PixelLab's edit_animation, and puts the
painted frames it returns back where the converter looks for them.

  crop SPECIES OUT_DIR
      Writes every frame the game uses (pixellab_to_c.py's FRAMES and
      CLIP_FROM) on one crop window: 64 x 64 when the union of all of them
      fits with room for a part, 80 x 80 otherwise. Frames go in batches of
      at most 16 (64 px) or 9 (80 px), the most one edit_animation call takes,
      as OUT_DIR/batchK/NN.png, with OUT_DIR/manifest.json saying which clip
      and frame number each one is and where the crop sits on the 128 x 128
      converter canvas.

  place SPECIES PART SIZE OUT_DIR
      Reads PixelLab's results from OUT_DIR/batchK/result/NN.png (the same
      names as the inputs), puts each back on the 128 x 128 canvas at the
      crop's corner and writes it to
      pixellab/SPECIES/parts/PART-SIZE/CLIP/N.png, N the base frame's own
      file number. PART is armour, weapon or swim; SIZE full or medium.

Needs Pillow. Run from anywhere; paths are taken from this file's folder.
"""
import json
import os
import sys

from PIL import Image

import pixellab_to_c as conv

HERE = os.path.dirname(os.path.abspath(__file__))
ART = os.path.join(HERE, "pixellab")
PARTS = ("armour", "weapon", "swim")
SIZES = ("full", "medium")
ROOM = 8  # pixels kept free above and in front of the body for the part


def used_frames(species):
    """(clip folder, file number) of every frame the game uses, each once:
    a clip drawn from another's frames (CLIP_FROM) adds that clip's."""
    seen, out = set(), []
    for _, clip in conv.ACTIONS:
        source = conv.CLIP_FROM.get(species, {}).get(clip, clip)
        folder = os.path.join(ART, species, source)
        names = sorted((n for n in os.listdir(folder) if n.endswith(".png")),
                       key=lambda n: int(n.split(".")[0]))
        span = conv.FRAMES.get(species, {}).get(clip)
        if span is not None:
            first, last = span
            names = names[first:None if last is None else last + 1]
        for n in names:
            key = (source, int(n.split(".")[0]))
            if key not in seen:
                seen.add(key)
                out.append(key)
    return out


def raw_on_canvas(path):
    """The frame as PixelLab drew it, on the converter's canvas, nothing
    removed: PixelLab should see the real art."""
    return conv.raw_canvas(path)


def crop(species, out_dir):
    frames = used_frames(species)
    images = [raw_on_canvas(os.path.join(ART, species, c, "%d.png" % n)) for c, n in frames]
    boxes = [im.getbbox() for im in images]
    x0 = min(b[0] for b in boxes)
    y0 = min(b[1] for b in boxes)
    x1 = max(b[2] for b in boxes)
    y1 = max(b[3] for b in boxes)
    size = 64 if (x1 - x0) + ROOM <= 64 and (y1 - y0) + ROOM <= 64 else 80
    if (x1 - x0) + ROOM > size or (y1 - y0) + ROOM > size:
        sys.exit("%s: frames span %d x %d, too big for an 80 px crop" % (species, x1 - x0, y1 - y0))
    per_call = 16 if size == 64 else 9
    # The union's bottom two pixels above the crop's, its middle in the
    # crop's middle, nudged back so the front keeps ROOM (animals face east).
    cx = (x0 + x1) // 2 - size // 2
    cx = min(cx, x1 + ROOM - size)
    cx = max(cx, x0)
    cy = y1 + 2 - size
    manifest = {"species": species, "size": size, "corner": [cx, cy], "batches": []}
    for b in range(0, len(frames), per_call):
        batch = frames[b:b + per_call]
        if len(batch) == 1:  # edit_animation wants 2 frames or more
            batch = frames[b - 1:b + 1]
        folder = os.path.join(out_dir, "batch%d" % (b // per_call))
        os.makedirs(folder, exist_ok=True)
        entries = []
        for i, (c, n) in enumerate(batch):
            im = images[frames.index((c, n))].crop((cx, cy, cx + size, cy + size))
            im.save(os.path.join(folder, "%02d.png" % i))
            entries.append([c, n])
        manifest["batches"].append(entries)
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("%s: %d frames, %d px crop at %s, %d batches" %
          (species, len(frames), size, manifest["corner"], len(manifest["batches"])))


def place(species, part, size, out_dir):
    if part not in PARTS or size not in SIZES:
        sys.exit("part is one of %s, size one of %s" % (PARTS, SIZES))
    with open(os.path.join(out_dir, "manifest.json")) as f:
        manifest = json.load(f)
    cx, cy = manifest["corner"]
    dest = os.path.join(ART, species, "parts", "%s-%s" % (part, size))
    count = 0
    for k, entries in enumerate(manifest["batches"]):
        for i, (clip, n) in enumerate(entries):
            path = os.path.join(out_dir, "batch%d" % k, "result", "%02d.png" % i)
            painted = Image.open(path).convert("RGBA")
            if painted.size != (manifest["size"], manifest["size"]):
                sys.exit("%s is %s, not the crop's size" % (path, painted.size))
            big = Image.new("RGBA", (conv.CANVAS, conv.CANVAS), (0, 0, 0, 0))
            big.alpha_composite(painted, (cx, cy))
            os.makedirs(os.path.join(dest, clip), exist_ok=True)
            big.save(os.path.join(dest, clip, "%d.png" % n))
            count += 1
    print("%s %s-%s: %d frames placed in %s" % (species, part, size, count, dest))


def main():
    if len(sys.argv) == 4 and sys.argv[1] == "crop":
        crop(sys.argv[2], sys.argv[3])
    elif len(sys.argv) == 6 and sys.argv[1] == "place":
        place(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5])
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
