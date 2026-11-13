#!/usr/bin/env python3
"""
Gets a species' frames ready for PixelLab's edit_animation, and puts the
painted frames it returns back where the converter looks for them.

  crop SPECIES OUT_DIR
      Writes every frame the game uses (pixellab_to_c.py's FRAMES and
      CLIP_FROM) in batches for one edit_animation call each: at most 16
      frames on a 64 x 64 crop, or 9 on 80 x 80 for frames too big for 64,
      each batch with its own crop window. As OUT_DIR/batchK/NN.png, with
      OUT_DIR/manifest.json saying which clip and frame number each one is
      and where each batch's crop sits on the 128 x 128 converter canvas.

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
ROOM = 8  # pixels kept free above the body for the part (a horn, a fin)


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


def union(boxes):
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            max(b[2] for b in boxes), max(b[3] for b in boxes))


def fits(box, size):
    """Whether a box fits a size x size crop with ROOM above it (for a horn
    or a fin) and 2 pixels at each side."""
    return box[2] - box[0] + 4 <= size and box[3] - box[1] + ROOM <= size


def corner(box, size):
    """The crop's corner on the canvas: the box centred across, its bottom
    two pixels above the crop's bottom."""
    return [(box[0] + box[2]) // 2 - size // 2, box[3] + 2 - size]


def crop(species, out_dir):
    """Frames go into batches greedily, in the order below: a frame joins the
    open batch while the batch's union still fits a 64 px crop and holds
    fewer than 16; a frame too big alone for 64 goes in an 80 px batch of at
    most 9. Each batch has its own crop window (every edit_animation call
    costs the same up to 128 px, so fewer calls is the saving)."""
    frames = used_frames(species)
    images = [raw_on_canvas(os.path.join(ART, species, c, "%d.png" % n)) for c, n in frames]
    boxes = [im.getbbox() for im in images]
    batches = []  # [size, [frame indices]]
    # The 64 px frames first, then the few that need 80, each group sorted by
    # how far forward the frame reaches (its box's right edge, then its left),
    # so frames that share a window sit next to each other: packing in the
    # game's order gave the wolf five calls, this gives fewer.
    def reach(i):
        return (boxes[i][2], boxes[i][0])
    order = sorted((i for i in range(len(boxes)) if fits(boxes[i], 64)), key=reach) + \
            sorted((i for i in range(len(boxes)) if not fits(boxes[i], 64)), key=reach)
    for i in order:
        box = boxes[i]
        size = 64 if fits(box, 64) else 80
        if not fits(box, size):
            sys.exit("%s %s/%d: %s too big for an 80 px crop" % ((species,) + frames[i] + (box,)))
        open_batch = batches[-1] if batches else None
        if open_batch is not None and open_batch[0] == size and \
                len(open_batch[1]) < (16 if size == 64 else 9) and \
                fits(union([boxes[j] for j in open_batch[1]] + [box]), size):
            open_batch[1].append(i)
        else:
            batches.append([size, [i]])
    manifest = {"species": species, "batches": []}
    for k, (size, members) in enumerate(batches):
        if len(members) == 1:  # edit_animation wants 2 frames or more
            members = members + members
        cx, cy = corner(union([boxes[j] for j in members]), size)
        folder = os.path.join(out_dir, "batch%d" % k)
        os.makedirs(folder, exist_ok=True)
        for i, j in enumerate(members):
            images[j].crop((cx, cy, cx + size, cy + size)).save(
                os.path.join(folder, "%02d.png" % i))
        manifest["batches"].append({"size": size, "corner": [cx, cy],
                                    "frames": [list(frames[j]) for j in members]})
    with open(os.path.join(out_dir, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)
    print("%s: %d frames in %d batches: %s" % (species, len(frames), len(batches),
          ", ".join("%d at %d px" % (len(m), z) for z, m in batches)))


def place(species, part, size, out_dir):
    if part not in PARTS or size not in SIZES:
        sys.exit("part is one of %s, size one of %s" % (PARTS, SIZES))
    with open(os.path.join(out_dir, "manifest.json")) as f:
        manifest = json.load(f)
    dest = os.path.join(ART, species, "parts", "%s-%s" % (part, size))
    count = 0
    for k, batch in enumerate(manifest["batches"]):
        cx, cy = batch["corner"]
        for i, (clip, n) in enumerate(batch["frames"]):
            path = os.path.join(out_dir, "batch%d" % k, "result", "%02d.png" % i)
            painted = Image.open(path).convert("RGBA")
            if painted.size != (batch["size"], batch["size"]):
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
