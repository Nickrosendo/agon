# Creature art from PixelLab

The window program draws a species from frames instead of its skeleton when
`src/gfx/art_data.c` has frames for it: all seventeen species since
2026-11-12. This folder holds the frames as PixelLab made them and the
scripts that turn them into that file.

| Path | What it is |
|---|---|
| `pixellab/<species>/<clip>/N.png` | PixelLab's east-facing frames, as downloaded: clips `idle`, `walk`, `run`, `graze`, `bite`, `attack`, `sleep` |
| `fetch_pixellab.py` | Downloads a character's east frames into `pixellab/<species>/` |
| `pixellab_to_c.py` | Writes `src/gfx/art_data.c`: each frame cut to its inked box, every pixel one of the engine's inks, the anchor and the eye per frame |
| `preview_art.py` | Draws `art_data.c` back as a picture, the eye in red, layers over their frames, to check a conversion before building (`preview_art.py OUT.png otter` for one species) |

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

## The last eleven (2026-11-12)

Made the same way (standard mode, quadruped, side view, size 48, east only).

| Species | PixelLab id | Template | Notes |
|---|---|---|---|
| lion | `f7ec282f-8354-489d-a44a-9da623ad1e37` | lion | its mane in the engine's mane inks (`DARK_IS_MANE`) |
| tiger | `4133f3f1-f3d1-430b-b68a-38b02352cef6` | lion | stripes come through as dark interior pixels |
| beaver | `16bb6c7b-83c0-4833-baaa-680975922f62` | cat | covers swim; bite from frame 3 (frame 2 holds a white object) |
| hippo | `4d2715f2-fa72-49b7-b73b-d3188c1c5a28` | bear | covers swim; its rest is the eating clip's first frame (the rest clip's skin broke into specks); pink muzzle stays fur; no game eye |
| rhino | `37b72934-2f64-4909-80f2-480cd02cc4d2` | bear | covers armour and weapon (its horn); rest from the eating clip's first frame; no game eye |
| hare | `f7565960-2379-4d41-94f1-c470804116ad` | cat | pink ears stay fur; rest from frame 1 (frame 0 has a leaf); attack frames 1–7 |
| gazelle | `d92d9d1b-1365-4700-8c9f-a8cc70065a75` | horse | run remade with the 6-frame template (the 4-frame one was blotchy); smoothed; no game eye |
| fennec | `17267a27-e6fa-4d8c-af0d-3e752dd8babb` | dog | pink ears stay fur; grass at the mouth dropped; eye search skips its ears |
| lemur | `641ea201-1442-4409-b9d1-23195cbd1cae` | cat | |
| deer | `a59eed15-a845-46c5-8bd4-4e0ec70763a8` | horse | smoothed; no game eye |
| zebra | `7be92e22-b780-4b8a-9113-0c1e8fad0e5f` | horse | stripes as the deepest fur shade; no game eye |

Template clips used: `walk-4-frames`, `running-4-frames` (the gazelle's
`running-6-frames`), the rest clip (`idle`; the bear's `idle-long`, the
horse's `rest-idle`), `eating` for graze where the template has it, and the
lion's `attack` and the horse's `running-headbutt` (the zebra's `attack`).
Custom motions, written as `action_description`, 4 frames for a bite and 8
otherwise (the dog's graze 6):

| Clip | Prompt |
|---|---|
| bite (hunters, beaver, hippo, lemur, fennec) | "snaps its jaws forward in a quick bite, then pulls its head back" |
| bite (hare) | "stretches its head forward and nips quickly, then pulls back" |
| bite (rhino, horse template) | "stretches its neck forward and bites, then pulls back" |
| graze (fennec) | "lowers head to the ground and nibbles grass, then lifts it a little" |
| attack (beaver, lemur) | "crouches low, then lunges forward with its head and front paws" |
| attack (fennec) | "crouches low, then pounces forward with front paws out" |
| attack (hare) | "crouches, then leaps forward kicking out with its front paws" |
| attack (hippo) | "opens its huge mouth wide and lunges forward, then closes it" |
| attack (rhino) | "lowers its head and charges forward horn first, then lifts its head" |
| sleep (lion, tiger) | "lies down on the ground on its belly, rests its head on its front paws and sleeps" |
| sleep (beaver, lemur, hippo, rhino) | "lies down flat on its belly on the ground, rests its head and sleeps" |
| sleep (hare) | "crouches down low on the ground, tucks its legs under its body, lays its ears back and sleeps" |
| sleep (fennec) | "lies down on the ground, curls up with its tail around its body and sleeps" |
| sleep (horse template) | "lies down on the ground with its legs folded under its body, lowers its head and sleeps" |

PixelLab's custom canvases vary (68 to 88 here) but always share the
template's centre, so the converter's 128 × 128 canvas still lines them up.

## Parts painted over the frames (2026-11-12)

A body whose genes show plates, a horn or a fin its frames lack is drawn
from its frames with the part on top, as a layer (`ArtSet.layer` in
`src/gfx/art.h`). PixelLab paints the part into the frames themselves
(`edit_animation`, the same instruction on every frame), and the converter
keeps only the pixels that changed.

| Path | What it is |
|---|---|
| `pixellab/<species>/parts/<part>-<size>/<clip>/N.png` | a painted frame on the converter's 128 × 128 canvas, N the base frame's own number; part `armour`, `weapon` or `swim`, size `full` or `medium` |
| `paint_parts.py crop SPECIES DIR` | the frames the game uses, cropped to one 64 (or 80) px window, in batches of 16 (or 9) for one `edit_animation` call each, with `manifest.json` |
| `paint_parts.py place SPECIES PART SIZE DIR` | PixelLab's results (`DIR/batchK/result/NN.png`) back on the canvas, into the parts folder |

Steps for one pair: `crop`; upload each batch (`get_upload_url`, one curl per
file); `edit_animation` per batch with the same description and seed;
download each job's frames into `batchK/result/`; `place`; regenerate
`art_data.c`, read what the converter prints on stderr, preview.

What the converter does with a painted frame: compares it with the base
frame as drawn (both without grass); a pixel is the part's when it is inked
where the base was empty or its colour moved by more than 48 (the three
channels' differences added). Only the largest touching group and pixels
within 3 of it are kept; any other changed pixel is printed as "body pixels
changed far from the part", and such a frame is cleaned or repainted before
it is kept. Inks: armour in the fur's five shades by its own lightness
across the part's range (the family colour stays, the bands between plates
stay darker lines; the fixed shell colours were tried first and turned a
purple otter tan), a horn in the five horn shades the same way, a fin in the
fur's shades against the clip's fur; near-black is the outline. In the game
a horn or fin pixel shows when any sample of a grid pixel hits it; armour,
broad like the body, keeps the frame's one-third rule. A frame where the part is
hidden gets an empty one-pixel layer.

### Pairs painted

| Pair | Prompt (`edit_animation` description) | Seed | Calls | Notes |
|---|---|---|---|---|
| otter, armour, full | "add hard grey armour plates over the otter's back and flanks, overlapping bands like an armadillo's shell, from the shoulders to the hips; keep the otter's head, legs, tail, outline and pose exactly as they are; change nothing else" | 1201 | 3 | |
| otter, weapon, full | "add one curved ivory horn growing up and forward from the otter's forehead, between its eyes and ears, like a small rhino horn; keep the otter's head, body, legs, tail, outline and pose exactly as they are; change nothing else" | 1202 | 3 + 2 | the four walk frames came back recoloured (the walk is a lighter brown than the otter's other clips); repainted with "keep the otter's brown fur colour, …" and seed 1212 |
| ~~wolf, swim, full~~ | "add a tall fin standing up along the middle of the wolf's back, like a fish's dorsal fin, …" | 1203 | 5 | removed 2026-11-13: a fin promises life in open water, which no creature has; swimming bodies keep their own body, so no swim layers are painted |

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

Rules set per species, each a set or table at the top of `pixellab_to_c.py`:

- `CLIP_FROM`: a clip drawn from another clip's frames (the armadillo's run
  from its walk, the hippo's and rhino's rest from their eating clip).
- `FRAMES`: which frames of a clip are used. The standing pose (anchor,
  size, eye) is the first rest frame used.
- `RED_IS_HORN`: reddish pixels are horn (the goat). `RED_IS_FUR`: reddish
  pixels stay fur (pink ears and muzzles).
- `DARK_IS_MANE`: pixels between the outline and 60 below the fur's lightness
  are mane, in the engine's five mane shades; only the largest patch of them
  is kept, so shadow on a leg is not mane. The art drawer outlines mane like
  fur.
- `EAR_ROWS`: top rows the eye search skips. `NO_EYE`: the game draws no eye
  (PixelLab's own stays, without blinking) where the finder took something
  else for one.
- `SMOOTH`: a lone shadow or light pixel with base on three of its four sides
  becomes base, for mottled frames.

And for every species: clearly green pixels (grass a motion drew) are dropped
before the body is found.
