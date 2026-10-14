# agon

*agon* (ἀγών): Greek for struggle or contest.

A terminal world where nobody decides who wins. Terrain is generated from a
seed, vegetation spreads by cellular-automaton rules, and creatures with
inherited neural-network brains graze, hunt, scavenge, reproduce and die.
Food chains, population cycles and families of related creatures are not
programmed in; they emerge.

Written in C11 with no dependencies beyond the C standard library and POSIX.
It is meant to be left running in a terminal, and can also print a single
snapshot of a world and exit.

**Status:** early work in progress. Nothing to build yet.

## Planned

- Seeded, repeatable worlds: same seed, same history.
- Full-screen 24-bit colour, two square pixels per character cell.
- Natural selection with no fitness function: energy is the only currency.
- Predation with no predefined roles: diet, size, speed and sight trade off,
  and each creature's food-chain level is measured from what it actually eats.
- Plants that inherit and mutate their own growth rules.
- A brain interface that lets an external model drive every creature in a
  world.

## Licence

MIT
