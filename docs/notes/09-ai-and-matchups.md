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

Integers only, every tick, from the `World` alone. The first version scored
every enemy per unit (`distance² − 12 × matchup + hp`) and sent each unit
at its own best; note 11 measured that against scripted opponents and it
lost to a ten-line focus-fire script. The AI as it stands now:

1. **One focus for the side.** Every enemy is scored for the team: the sum
   over our units of `distance² − 12 × matchup`, plus the enemy's health
   times our unit count. The lowest is the focus and everyone converges on
   it, so damage lands on one unit at a time. Ties go to the lower id,
   which is the order units are stored in.
2. **Fight what is already in reach.** A unit with an enemy inside its range
   and in line of sight attacks that one (best matchup, then most wounded)
   rather than walking away from it to join the focus.
3. **Nobody charges in alone.** A unit more than five tiles² ahead of its
   slowest teammate, measured towards the focus, holds its own tile until
   the gap is under three tiles². Two thresholds, so it does not flap
   between orders; the hold is a Move to its own tile, which is also how
   the AI recognises its own holding state without keeping any.
4. **Archers shoot, then step back.** With a melee unit inside two tiles an
   Archer shoots if the shot is ready and otherwise spends the cooldown
   stepping to the neighbouring floor tile farthest from that unit
   (diagonals only when both sides are open). Cornered, it fights. The
   version that retreated regardless of cooldown lost shots and ground
   against every opponent, so it went.

An order is only emitted when it changes what the unit is already doing,
so a 600-tick replay does not fill with repeats.

No random numbers and no `float`: the same `World` gives the same orders on
every machine. That matters because the AI's commands go into the replay
and the CI's `replay` tool now plays random Blue against AI Orange, so the
1,000-match comparison between MSVC, Apple clang on arm64 and WebAssembly
covers the AI as well as the simulation.

## Measured

`arena 1000 600`, Blue random against Orange the AI: **995 / 1000** (was 978
before the five steps in note 11), mean match length 114 ticks. Random
orders are a weak yardstick, so the probe in note 11 also plays four
scripted opponents; the table there is the honest one (focus fire 738-257,
the AI against itself 546-439 with one timeout).

`tests/test_ai.cpp` asserts a floor of 70 % over 200 seeds against random,
well under the measurement, so a balance tweak does not break the build
over a few seeds; it also pins the team focus, the in-reach rule, the hold
and release thresholds, the Archer's stutter step, the matchup choice, the
wounded-target choice, and that a second call on the same world repeats
nothing.

**What it is not.** No look-ahead, no retreat when outnumbered, no use of
the walls as cover on purpose, no stepping aside for a teammate in a
corridor. It is enough to beat the obvious scripts and to punish a player
who sends an Archer into a Soldier, and it is still short enough to read in
one sitting.

**Questions this answers in an interview**
- Why must the AI be deterministic if it is not part of the simulation?
  (Its commands are; a replay verified elsewhere must contain the same ones,
  and the CI compares matches that the AI played.)
- How do you keep an AI from flooding the replay? (Emit only changes.)
- What did the matchup bonus change in the hashes? (All of them: it is a
  simulation rule.)
