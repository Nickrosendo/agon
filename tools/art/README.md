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

## The other species (2026-11-11)

All made the same way: standard mode, quadruped, side view, size 48, east
only. Template clips where the template had one, custom motions otherwise;
`pixellab_to_c.py`'s `FRAMES` and `CLIP_FROM` say which frames are used.

| Species | PixelLab id | Template | Notes |
|---|---|---|---|
| fox | `142e60b7-0ba7-44c4-a8c0-7e652e25173a` | dog | like the wolf |
| goat | `16f6d35c-a7f8-4026-ad9b-93751175b275` | horse | horse template's eating and head-butt; its standing "sleep-cycle" (with a dream bubble) replaced by a custom lying-down sleep; red-brown horns drawn in horn colours |
| otter | `91be7bbd-14d4-496f-8644-e986097cadb4` | cat | covers the swim gene; attack cut at frame 5 (a cyan swirl after) |
| armadillo | `3ae0bad6-7d3f-4f39-be0e-83de9e0d54ac` | cat | covers armour; its run came out broken, so the run uses the walk's frames |
| elephant | `677ccd73-aba5-4ab7-94b3-37a769ac5ca9` | bear | covers armour and weapon (tusks); bite cut at frame 2 (an object appears after) |

## Adding a species

1. In PixelLab, `create_character` the same way (standard, quadruped, the
   nearest template, side view, size 48). Animate `east` only, giving each
   animation the clip's name (`animation_name`): `walk`, `run`, `idle`,
   `graze`, `bite`, `attack`, `sleep`. Templates when the character's
   `available_animations` has one, custom motions otherwise.
2. `python3 tools/art/fetch_pixellab.py CHARACTER_ID NAME` (it refuses while
   any of the character's jobs still runs).
3. Add the name to `SPECIES` in `pixellab_to_c.py` with the parts its frames
   cover, look at the clips, set `FRAMES` (and `CLIP_FROM`, `RED_IS_HORN`)
   as needed, regenerate, preview.
4. `make test`: the lab test's pinned picture changes; look at the lab (`b`)
   and re-pin it.

## What the converter does to every frame

- Puts it on one 128 × 128 canvas (PixelLab's canvases share a centre).
- Keeps only the largest group of touching pixels: dream bubbles, dust and
  loose dots go.
- Sorts pixels into inks by lightness against the clip's most common fur
  lightness: base within 10, shadow and light to 25 and 30, then deep, high,
  belly (50 lighter), outline (very dark). Reddish pixels are the mouth, or
  horn for `RED_IS_HORN` species, unless the fur itself is reddish.
- Finds the eye on the standing frame and places it on every frame of a clip
  from the nose, or on none.
