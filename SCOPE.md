# Spore Lantern — scope

Written before any game code. Design changes made later are recorded at the bottom.

## Pitch

You are a lantern-bearer in a small, dark cave. Your lantern only lights a
little circle around you. Gather three glowing spores, brew them into a light
potion, and throw it at the slime that guards the exit.

## Core loop

1. **Start** — title screen. `Enter` starts; if `save.txt` exists the run continues from it.
2. **Explore** — run and jump through one hand-made cave (about 70 x 22 tiles).
3. **Collect** — pick up three glowing spores, each behind a small challenge
   (a walk, a climb, a spike pit).
4. **Brew** — with three spores, press `E` to turn them into one light potion.
5. **Throw** — press `F` to throw the potion the way you face. It arcs under
   gravity and bursts on the first thing it hits, or after 2 seconds.
6. **Defeat** — a burst close to the slime destroys it and unseals the exit door.
   A miss is not a dead end: the spores regrow where they were.
7. **Win** — touch the open door: win message, then the level resets.
8. **Lose** — three hearts. Slime contact and spikes cost one each. At zero the
   lantern goes dark: lose message, then restart from the last shrine save
   (or from the start if there is none).
9. **Shrine** — touching it saves position, spores, potion and hearts to
   `save.txt`, and heals. The file is loaded on start if present.

## Controls

| Key | Action |
| --- | --- |
| `A` / `D` or arrows | move |
| `Space` / `W` / `Up` | jump (hold for higher) |
| `E` | brew potion (needs 3 spores) |
| `F` | throw potion |
| `R` | restart from last save |
| `N` (title) | new game, erases the save |
| `Esc` | back to title / quit from title |

## Must have (the complete loop)

- Player movement with tile collision, jump.
- Three spores, HUD counter, brew, throw, potion arc and burst.
- Slime that blocks the exit and hurts on touch; dies to a potion burst.
- Exit door, win message, reset.
- Hearts, hazards, lose message, restart.
- Shrine save/load via `save.txt`.
- README (with engine commit), ASSETS.md, FRICTION.md.

## Polish (in this order, after the loop is complete)

1. Own pixel art for everything; animated player and slime.
2. Darkness with light sources (lantern, spores, shrine, potion).
3. Sound effects for every action.
4. Particles, screen shake, hint text.
5. Ambient music loop.
6. Parallax background and cave decoration.
7. Window resize handling.

## Cut list (first to go if time runs short, top goes first)

1. Window resize handling (fixed 960x540 is acceptable).
2. Parallax background and decoration sprites.
3. Ambient music.
4. Particles and screen shake.
5. Multi-source lighting (fall back to no darkness at all).
6. Slime hop AI (fall back to a simple patrol).
7. Animated sprites (fall back to single frames).

Never cut: the loop in "Must have".

## Design changes

Recorded as they happen; see the end of README.md for the final list.
