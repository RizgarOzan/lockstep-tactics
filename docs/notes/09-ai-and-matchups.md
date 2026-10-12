# 09 — The AI, and why Soldiers beat Archers

**What:** `core/src/ai.cpp`, the opponent the viewer and the CI play against,
and a small change to the simulation: a damage bonus around the triangle
Soldier → Archer → Knight → Soldier. Before this, Orange picked a random unit
on a quarter of the ticks and sent it somewhere random; it was a load
generator, not an opponent.

## The matchup

`damage(attacker, target)` is the attacker's base damage plus 2 when the
pair is the strong side of the triangle. It is a bonus, not a multiplier, so
an Archer (6 hp) still survives one Soldier hit (3 + 2 = 5): a mistake costs
a unit over a few ticks, not at once. This is a simulation change, so every
replay hash moved; the commit says so.

## The AI

Each tick, for each of its units, the AI scores every enemy with integers
only:

```
score = distance²(tiles) − 12 × matchup + enemy hp     (lower is better)
```

so the closest enemy wins unless a better matchup or a wounded target is
within about three tiles. Ties go to the lower id, which is the order units
are stored in. An Archer that has a melee unit on the next tile and no shot
ready steps one tile away first, then shoots when the cooldown ends. An
order is only emitted when it changes what the unit is already doing, so a
600-tick replay does not fill with repeats.

No random numbers and no `float`: the same `World` gives the same orders on
every machine. That matters because the AI's commands go into the replay
and the CI's `replay` tool now plays random Blue against AI Orange, so the
1,000-match comparison between MSVC, Apple clang on arm64 and WebAssembly
covers the AI as well as the simulation.

## Measured

`arena 1000 600`, Blue random against Orange the AI:

| | wins |
|---|---|
| AI (Orange) | 978 / 1000 |
| random (Blue) | 21 / 1000 |
| draw | 1 / 1000 |

Mean match length 107 ticks. The same numbers come out of the WebAssembly
build. `tests/test_ai.cpp` asserts a floor of 70 % over 200 seeds, well
under the measurement, so a balance tweak does not break the build over a
few seeds; it also pins the matchup choice, the wounded-target choice, the
Archer's step back and that a second call on the same world repeats nothing.

**What it is not.** No pathfinding (units slide along walls, note 04), no
group tactics, no look-ahead. It is enough to punish a player who sends an
Archer into a Soldier, and simple enough to read in one sitting.

**Questions this answers in an interview**
- Why must the AI be deterministic if it is not part of the simulation?
  (Its commands are; a replay verified elsewhere must contain the same ones,
  and the CI compares matches that the AI played.)
- How do you keep an AI from flooding the replay? (Emit only changes.)
- What did the matchup bonus change in the hashes? (All of them: it is a
  simulation rule.)
