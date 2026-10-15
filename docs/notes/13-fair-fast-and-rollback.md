# 13 — A fair tick, cheaper paths, and rollback

**What:** the three things the probe in note 11 and the benchmark in note
12 left on the table. The tick no longer favours low unit ids; a step costs
about half of what the first pathfinding did; and an online match can run
without input delay, re-running ticks when the other side's orders arrive.
The AI also got a rule for two teammates meeting in a one-wide gap, and
the viewer dots the path a unit is about to walk.

## The tick was not fair

With the same AI on both sides Orange won 546 to 439. Units acted in id
order inside a tick and each one saw the moves of the ones before it: unit
2 measured its range against where unit 1 had just stepped, and Blue has
the lower ids. Now every unit decides its move or its hit from the units as
they stood when the tick began, and the moves land afterwards in id order.
The one thing id order still settles is a tile two units want in the same
tick, which goes to the lower id. Mirror after the change: 496 to 489.

`tests/test_combat.cpp` has the case that used to fail: two equal Soldiers,
three tiles apart, each ordered at the other, must take their first hit on
the same tick.

## Paths that follow the line, and a search that stops early

Two changes to movement, one of them a bug fix found while measuring.

A unit whose straight segment to the goal is clear walks it with no search.
`line_clear()` is a stricter `line_of_sight()`: every tile the segment
touches must be floor, including both tiles beside a diagonal step. The
first version of this kept the old stride, which moved up to a full speed
on each axis and therefore cut 45 degree corners off the checked line; a
unit could leave the corridor, get a path step back into it, and oscillate
between two tiles for a whole match (513 of 1,000 timeouts against the
hold-spawn script, from 2). The stride now follows the segment: the longer
axis moves by `speed`, the shorter by the matching fraction in fixed point.

When the line is not clear, `next_step()` runs A* with the Manhattan
estimate instead of filling the map breadth first. The open list is a set
of buckets by f = g + h, each a FIFO, with a tile of equal f but smaller h
going to the front; no heap, no float. Among several shortest paths that
picks the one heading for the goal first, then the neighbour order. The
tick snapshot is a fixed array rather than a vector.

`bench 200 600` under gcc: median about 1.0 µs per tick, from 1.85 µs with
the breadth-first search (note 12's 70 ns was before any pathfinding).
Still mostly the search; the next step would be caching the distance field
per goal tile within a tick.

## Rollback

`core/include/lockstep/rollback.hpp` is the second way to play online, on
the same frames as `Lockstep`. A local order runs on the tick it is given.
The peer's orders for a tick arrive later; when they do, the state is
restored to the last tick both sides' orders are known for and the ticks
since are run again with everything now known. There is no prediction of
the peer beyond "no new orders", which is exactly what a tick without
orders means in `step()`: units keep doing what they were doing. A tick is
*confirmed* once both sides' orders for it are in; only confirmed ticks
have hashes the two sides compare, and the saved replay is the confirmed
part. If the present runs more than 30 ticks ahead of the confirmed tick,
`advance()` refuses and the viewer says so.

`tests/test_rollback.cpp` runs two sides for 200 ticks with frames
delivered 0, 1, 3 or 7 ticks late at random, then checks both sides'
confirmed hashes against a plain simulation of the same orders with no
network at all; they are identical, and nothing was re-run when nothing
was late. A forged frame claiming a different hash for a confirmed tick is
reported at that tick.

In the browser the host ticks *rollback* in the room box and the choice
travels with the offer. The top bar shows the confirmed tick and how many
ticks have been re-run. Tested with two tabs on one machine: with the
guest tab in the background (its frame loop crawls) the host ran ahead to
the 30-tick limit, waited, and re-ran several hundred ticks as the guest's
frames came in bursts; an order given on the host walked the unit on the
guest once it caught up; no desync.

## The AI, briefly

Two teammates heading into each other in a one-wide gap used to stand
there until the match ended. A unit about to walk into a teammate heading
for its own tile, or into the same tile from the other side, now moves to a
free tile beside the line; when both see the conflict only the higher id
yields. Measured absurdities, for the record, over 300 matches against
random orders: 5 % of moving ticks reverse direction (detours), 7 % of
attack-moving ticks end farther from the target than they began (walls in
between), one target switch A-B-A within six ticks every two or three
matches. A rule that kept the current in-reach target removed a quarter of
those switches and lost more matches than it saved, so it is not in.

**Questions this answers in an interview**
- Where does order inside a tick leak into fairness, and how do you stop it?
  (Decide from a snapshot; apply afterwards; name the one tie that remains.)
- Why can a straight-line check and a stride disagree? (The check follows
  the segment; the stride must too.)
- What does rollback predict, and what does it confirm? (Nothing beyond
  "no new orders"; a tick with both sides' orders.)
