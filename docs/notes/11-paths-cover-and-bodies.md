# 11 — Paths, cover and bodies

**What:** three simulation rules added after measuring the AI against
scripted opponents (note 09 has the AI side of the same work): units walk
the shortest path instead of a straight line, a wall between attacker and
target blocks the shot, and a tile holds one unit. Every one of them moved
every replay hash, and each commit says so.

## Why: the numbers before

The AI from note 09 won 978 of 1,000 against random orders, which sounded
fine until it played anything else. A probe ran it against five opponents
for 1,000 seeded matches each: random orders, a copy of itself, a ten-line
"everyone attacks their Archer first" script, a "hold the spawn until they
come" script, and a script whose Archer keeps three tiles away.

| Opponent | AI wins | opponent wins | timeouts (600 ticks) |
|---|---|---|---|
| random | 978 | 21 | 3 |
| itself | 408 | 334 | 278 |
| focus fire | 267 | 465 | 288 |
| hold spawn | 414 | 35 | 576 |
| archer kites | 607 | 249 | 215 |

The timeouts were the tell. In 219 of the 278 mirror timeouts a unit had
not moved for 50 ticks or more; in 208 all six units were still alive. The
old `walk()` moved straight at the target and slid along a wall one axis at
a time, so a target on the same row behind a wall meant leaning on the wall
until the match ended. Seed 154 against random orders: all three orange
units stacked on tile (12,7) against a wall while Blue strolled over and
killed them one by one.

## Paths

`next_step()` runs a breadth-first search from the goal over floor tiles,
four-connected, and `walk()` strides towards the first tile of that path
each tick (or straight at the target once on its tile). Ties are broken in
a fixed neighbour order (up, left, right, down), so every machine picks the
same tile; the search touches at most 256 tiles and runs per unit per tick
without showing up in the timings. An unreachable goal falls back to the
old straight line, which at worst stands still.

After this one change: zero timeouts against every opponent, the mirror
476/505/19, focus fire 307-681 the other way, random 997.

## Cover

The range check only measured distance, so an Archer shot through a wall it
could not walk through. `line_of_sight()` walks an integer Bresenham line
over the tiles between the two units; a wall on it means no shot, and the
attacker keeps walking round until it can see. The numbers barely moved,
which is the expected result for a rule that changes where a shot is fired
from rather than whether it is; the point is that walls now mean the same
thing to an arrow as to a foot.

## Bodies

Units stacked on one tile (the three on (12,7) above were standing inside
each other). Now a path treats other units' tiles as walls, except the goal
tile, and a unit stops short of entering a goal tile someone else stands on.
A Knight in a one-tile gap is a door. The map generator also flood-fills
from the spawn and turns any floor it cannot reach into wall, so no order
can point into a pocket; this uses no random draws, so the one-draw-per-tile
rule from note 03 still holds.

Collision costs the AI a little: three attackers cannot all stand where they
like, corridors form queues, and a unit holding for a slow teammate can be
the thing blocking it. Focus fire went from 852-143 to 738-257 and one or
two matches in a thousand now time out with two teammates facing each other
in a one-wide gap. I kept it: the game is better for it and the AI is where
that should be fixed, with a rule that steps aside, not by letting bodies
pass through each other.

## After all five steps

| Opponent | before | after |
|---|---|---|
| random | 978-21 | 995-5 |
| itself | 408-334, 278 timeouts | 546-439, 1 timeout |
| focus fire | 267-465 | 738-257 |
| hold spawn | 414-35, 576 timeouts | 973-26, 2 timeouts |
| archer kites | 607-249 | 675-313 |

The mirror is not 500-500: Orange wins more with the same AI on both sides,
which can only come from the simulation's tie-breaks (lower ids act first
within a tick, and Blue has the lower ids). That is a fairness bug worth a
note of its own; it is not a determinism bug, since both sides agree on it.

**Questions this answers in an interview**
- Why BFS and not A*? (256 tiles, four neighbours, no heuristic needed;
  the fixed tie order matters more than the speed.)
- Where did the "AI is bad" problem actually live? (Mostly in the
  simulation: no paths, no cover, no bodies.)
- How do you make a game rule change without breaking determinism? (It
  must be integers, it must be ordered, and every hash is expected to move.)
