# 16 — Walking to the objective together

**What:** the loss note 15 left open. Against the kiting Archer script the
AI went from 552-427 (note 14, no objective play) to 486-481 once it
started walking to the objective. This note gets it back, to 580-384,
with one rule and one number, and nothing else in the table goes down.

## What the losses looked like

A scratch tool ran the AI against the script 1,000 times and split the
matches into wins and losses: tick of first contact, who was alive then,
who died first. In the losses the first unit to die was the Soldier, early,
and alone. The reason is in note 15 itself: the walk to an objective tile
had no gap wait, "because it is about not arriving at a fight alone". The
Soldier is the fastest unit, so on the walk it pulls ahead; the script's
melee attacks whatever is nearest, so it meets the Soldier on its own
while the Knight is still three tiles back. Taking the sit rule out
entirely gave 544-436, which said the tempo was lost on the walk, not in
the sit.

## The rule, and the number

A unit walking to its objective tile now waits for the group the same way
the walk on the focus does (note 09), with a tighter gap: more than
two tiles² ahead of the slowest teammate it holds its tile, under one it
goes on. The number is `near <= 64`: the wait only applies while the
nearest enemy is within eight tiles. Farther off, the walk is free, as it
was.

The gate is not a tuning knob, it is what makes the rule work at all.
Without it, "hold spawn" (a script that never leaves its corner) went from
1000-0 to 145 wins and 855 timeouts: with nobody near, the three units
waited for each other across the map until the clock ran out, and the AI
against itself timed out 114 matches the same way.

The second change is in the fight walk: a unit an enemy can already hit
does not wait for the group. Standing still with nothing to hit back,
under an Archer's fire, is a way of losing slowly. On its own this is
worth 25 matches against focus fire and costs 15 against the kiter; with
the post wait it is worth 7 and 31. The two are kept together because
each one's cost is the other one's gain.

## Tests, and what broke them on purpose

Two cases in `test_ai.cpp`. A Soldier four tiles ahead of its Knight, with
an enemy six tiles off that can hit nobody and the hold count level,
holds its tile while the Knight gets an objective tile; move the enemy to
eleven tiles away and the Soldier gets an objective tile too. And a
Soldier three tiles from an enemy Archer, far ahead of its Knight, attacks
instead of holding.

| Change | Caught by |
|---|---|
| near gate removed (always wait on the post walk) | "walking to the objective with an enemy near…" (the far half), and two note 15 cases: units no longer reach their tiles |
| under-fire exemption removed | "a unit being shot at does not stand and wait…" |

## Measured

`probe 1000 600` with gcc, Blue the script, Orange the AI. Each column is
a build from the same tree with one line changed; timeouts in brackets.

| Opponent | note 15 | under-fire only | post wait only | post wait, no gate | **both** |
|---|---|---|---|---|---|
| random | 998-2, 133 by holding | 999-1 | 997-3 | 996-3 | **998-2, 59 by holding** |
| itself | 483-486 | 490-477 | 491-487 | 438-430 [114] | **495-476** |
| focus fire | 800-194 | 825-171 | 806-186 | 796-196 | **813-177** |
| hold spawn | 1000-0, 994 by holding | 1000-0 | 1000-0 | 145-0 [855] | **1000-0, all by holding** |
| archer kites | 486-481 | 471-488 | 549-415 | 575-399 | **580-384** |

Fewer random matches are won by holding now (59 against 133), because a
side that groups up near an enemy fights it rather than walking past it
to sit; the win count is the same. The rule the backlog set for this
change was that no column may go down, and none does: random and hold
spawn are level, the other three are up.

`arena 1000 600`: 998 / 2 / 0, 59 won on the objective, mean length 111
ticks. gcc and WebAssembly agree on `replay 1000 600`, 1000 of 1000
hashes, and both run the 73 test cases green.

One trap, for next time: a change and a rebuild inside the same second
can leave the old object in the build, because make compares file times
at one-second granularity. The ablation builds above `touch` the file and
wait a second first; the first sweep did not, and its numbers for the
shipped variant did not reproduce from a clean build.

**Questions this answers in an interview**
- The AI lost tempo after it learned to play the objective. How did you
  find where? (Split wins and losses by who died first and when: the
  Soldier, early, alone, on the walk, not in the sit.)
- Why gate the wait on distance instead of always grouping up? (Without
  the gate the side waits for itself across an empty map; hold spawn went
  from 1000 wins to 855 timeouts.)
- How do you know a one-line change did not cost you somewhere else?
  (Five scripted opponents, 1,000 seeds each, before and after, and the
  rule that no column may go down.)
