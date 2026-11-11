# Creature art from PixelLab

The window program draws a species from frames instead of its skeleton when
`src/gfx/art_data.c` has frames for it (only the wolf so far). This folder
holds the frames as PixelLab made them and the two scripts that turn them
into that file.

| Path | What it is |
|---|---|
| `pixellab/<species>/<clip>/N.png` | PixelLab's east-facing frames, as downloaded: clips `idle`, `walk`, `run`, `graze`, `bite`, `attack`, `sleep` |
| `pixellab_to_c.py` | Writes `src/gfx/art_data.c`: each frame cut to its inked box, every pixel one of the engine's inks, the anchor and the eye per frame |
| `preview_art.py` | Draws `art_data.c` back as a picture, the eye in red, to check a conversion before building |

Regenerate after changing frames (Pillow needed):

    python3 tools/art/pixellab_to_c.py tools/art/pixellab > src/gfx/art_data.c
    python3 tools/art/preview_art.py /tmp/preview.png

## The wolf (2026-11-11)

PixelLab character "agon wolf standard", id `b69dadb3-b263-4380-b335-50514eb90b5b`:
`create_character` in standard mode, quadruped, `dog` template, side view,
size 48 (a 68×68 canvas), 4 directions. Only `east` is used; the game
mirrors it for facing left.

| Clip | How it was made | Frames used |
|---|---|---|
| idle | template `idle` | all 8 |
| walk | template `walk-4-frames` | all 4 |
| run | template `running-4-frames` | all 4 |
| graze | custom (v3) "lowers head to the ground and nibbles grass…", 6 frames | 1–6 (0 is the standing pose PixelLab keeps) |
| bite | custom "snaps its jaws forward in a quick bite…", 4 frames | 1–4 |
| attack | custom "crouches low, then pounces forward…", 8 frames | all 9 |
| sleep | custom "lies down on the ground, curls up…", 8 frames | 5–8 (lying down) |

Custom clips come on an 84×84 canvas, template ones on 68×68: they share a
centre, which is how the converter lines them up.

## Adding a species

1. In PixelLab, make the character the same way (standard, quadruped, the
   nearest template, side view) and the same clips, east only.
2. Download each clip's frames into `pixellab/<name>/<clip>/` as `0.png`,
   `1.png`, ...
3. Add the name to `SPECIES` in `pixellab_to_c.py` (and to `SLEEP_FRAMES`,
   after looking at which frames are lying down), regenerate, preview.
4. The game picks it up: `art_clip` looks the species up in `ART_SETS`.
