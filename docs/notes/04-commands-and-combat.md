# 04 — Commands and combat

**What:** `step(world, commands)` now plays a real tick. A `Command` gives
one unit a Move (to a tile) or Attack (a unit) order; units walk at their
speed, attack in range with a hit roll from the world's `Pcg32`, wait
`kAttackCooldown` ticks between attacks, and die at 0 hp.

**One tick, three phases.**
1. *Orders.* Commands are sorted, then applied. Orders for a unit the
   player does not own, or that no longer exists, are dropped.
2. *Act,* in unit id order: walk, or roll to hit. A hit is only recorded.
3. *Resolve:* all recorded damage lands at once, then the dead are removed
   (`std::erase_if` keeps the vector's order).

**Sorting on every field, not on the unit.** Two players' packets reach each
machine in whichever order the network delivers them, so commands are put in
a canonical order first. The obvious key, (player, unit id) with a stable
sort, is not enough: if a player sends two orders for one unit in the same
tick, a stable sort keeps their *arrival* order, and "the last order wins"
picks a different order on each machine. Sorting on all fields makes the
order total. The test sends two orders for unit 1 in both arrival orders; the
(player, unit) stable sort fails it (checked once by hand).

**Simultaneous damage.** If damage were applied as each unit acts, two units
that kill each other in one tick would not die together: the lower id strikes
first, the other is dead before its turn. That is deterministic, but it makes
unit ids a hidden advantage. Recording hits and applying them after everyone
has acted removes that; replacing it with immediate damage fails the
"both die" test (checked once by hand). In 1,000 random matches, 9 end with no
unit left standing, which is that rule at work.

**Movement is per axis.** x first, then y, each step clamped to the unit's
speed and refused if it would enter a wall. A blocked x still lets the unit
slide along y. Units may share a tile: there is no collision between units
yet. Attackers chase their target's *current* position, which a lower id may
already have changed this tick; that order dependence is deterministic and
deliberate (the unit id order is the rule).

**Measured:** 2,199 test assertions pass under gcc 16 and Emscripten 6. In
1,000 seeded matches of 600 ticks with random orders, both builds produce the
same final hash for every match, and all 1,000 hashes are distinct. Units left
at the end: 0 in 9 matches, 1 in 251, 2 in 375, 3 in 365.

**A test that only crashed.** Removing the cooldown first made the cooldown
test crash instead of fail: the knight died, and the test read its hp through
a null pointer. It now `REQUIRE`s the unit exists before reading it, so the
same mutation shows up as a clean failure.

**Questions this answers in an interview**
- Why canonicalise inputs in a lockstep game, and why is a stable sort on the
  obvious key not enough?
- Simultaneous or sequential resolution, and what does each do to fairness?
- How do you know a test would catch the bug it is named after? (Break the
  code on purpose and watch it fail.)
