# agon

*agon* (ἀγών): Greek for struggle or contest.

A terminal world where nobody decides who wins. Terrain is generated from a
seed, vegetation spreads by neighbour rules that it inherits, and creatures
with inherited neural-network brains graze, hunt, scavenge, reproduce and die.
Food chains, population swings and families of related creatures are not
programmed in; they emerge.

Written in C11 with no dependencies beyond the C standard library and POSIX.
Leave it running in a terminal, or have it print one snapshot of a world and
exit.

## Build and run

You need a C compiler and `make` on Linux.

```sh
make release
./build/release/agon
```

To install it (default `/usr/local`, change it with `PREFIX`):

```sh
sudo make install
sudo make uninstall
```

`make test` runs the tests with the memory checkers on.

The display uses 24-bit colour and two square pixels per character cell, so
it wants a terminal with true colour and a font with the half-block character.

## Keys

| Key | What it does |
|---|---|
| space | pause |
| `+` `-` | faster, slower |
| `r` | a new world from a new seed |
| `i` | inspect: arrows or `hjkl` move the cursor (`HJKL` by 5), and the panel shows the plant and creature under it |
| `s` | save, if started with `--save` |
| `q` | quit (and save, if started with `--save`) |

## Options

```
--seed N       the world's seed: a whole number, or 0x hexadecimal
--size WxH     world size in cells (default: fit the window; 80x40 with --once)
--speed N      ticks per second (default 10)
--once         print one snapshot after --ticks ticks and exit
--ticks N      ticks to run before the snapshot (default 2000)
--no-floor     never place new random creatures
--config PATH  read settings from a file
--save PATH    write the world there on quit and on s
               (with --once: after the last tick)
--load PATH    resume a saved world
--settings     print every setting and its default, and exit
-h, --help     help
--version      the version
```

The same seed and settings give the same world, tick for tick, on the same
build. A snapshot works anywhere, even in a pipe:

```sh
agon --once --seed 3 --ticks 5000 --size 100x60
```

Ctrl-C ends the program without saving; only `q` and `s` save.

## Settings

Every number in the simulation can be changed from a text file:

```sh
agon --config my.conf
```

```ini
# my.conf: lines are name = value; anything left out keeps its default
mutation_rate = 0.1
max_age = 3000
```

[`agon.conf.example`](agon.conf.example) lists all of them with their defaults
and ranges (`agon --settings` prints the same list). A mistake stops the
program with the file's name, the line number and what was wrong.

A save file holds its world's settings, seed and size, so `--load` cannot be
combined with `--config`, `--seed`, `--size` or `--no-floor`. Save files are
for the build that wrote them; a build with a different format refuses them.

## How the world works

1. Cells are water, rock or soil, from noise that wraps around the edges. Soil
   has a fertility.
2. Every plant carries its own rule: how many planted neighbours let an empty
   cell sprout, and how much crowding it tolerates. A sprout copies a planted
   neighbour's rule, sometimes with one bit flipped.
3. Vigorous rules spread faster, but they feed less and die younger. Plants
   age, wither and leave bare soil.
4. Each creature inherits a genome: its size, speed, sight, digestion of
   plants and of meat, its colour, and the weights of a small neural network.
5. Each tick, the network turns what the creature senses into one action: rest,
   turn left, turn right, move, bite or split.
6. Energy is the only currency. Living costs energy, and bigger, faster,
   farther-seeing bodies cost more. At zero, a creature dies and leaves carrion.
7. A creature with enough energy can split. The child gets a mutated copy of
   its genome. No fitness function exists: what lives long enough to split
   spreads.
8. Hunters attack plant-eaters, and bigger attackers hit more often. Kills
   become carrion, which rots into the soil and sends out a scent.
9. A creature's food-chain level is measured from what it actually eats: a
   plant-eater sits at about 2, something that eats plant-eaters at about 3.
10. A floor of random newcomers keeps a world from staying empty (`--no-floor`
    turns it off), and caps on plant-eaters and the population keep the
    simulation fast.

## Licence

MIT
