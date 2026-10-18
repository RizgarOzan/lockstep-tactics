# 15 — Playing the objective

**What:** the second half of roadmap item 13. The AI now goes for the
objective from note 14: it pushes off an enemy standing on it, and sits on
it itself while nobody can hit it and it is level or ahead on the hold
count. The viewer shows the four tiles and a hold bar per side, and a
`probe` tool plays the AI against the five scripted opponents from note 11,
so a change like this one is measured against more than random orders.

## Three rules, in `ai_orders()`

**Push.** An enemy on the objective counts 64 tiles² nearer per unit of
ours in the team focus score (note 09). Every tick it stands there alone is
a tick towards its win, so the side converges on it before anything else;
64 is well above the distances that otherwise decide the focus.

**Sit.** With no enemy on the objective, no enemy able to hit any of our
units from where it stands (`can_hit` the other way round: range and line
of sight), and `held[us] >= held[them]`, every unit with nothing in reach
walks to an objective tile of its own and stands on it (a move to its own
tile, the same "holding" order note 09 uses for the slow-teammate wait).
Tiles are assigned deterministically: a unit already on a tile keeps it,
the rest take the nearest free tile in id order. The gap wait does not
apply to these walks; it is about not arriving at a fight alone.

**Fight.** Behind on the count, or with an enemy in reach of anyone, the
side fights exactly as before. Sitting still while an Archer shoots from
four tiles would lose the match sitting; that was the first version, and
the table below has what it cost.

## Tests, and what broke them on purpose

Four new cases in `test_ai.cpp`: the enemy on the objective is the focus
however far it is; level on the count with the objective empty, three
units get three different objective tiles, hold them without repeating the
order, and `held` ticks up; behind on the count they attack instead; and
over 100 seeded matches against random orders at least one is won by
holding. The test room now starts with Blue a tick ahead, so the fighting
tests from notes 09 and 11 still test fighting; the objective tests reset
that.

| Change | Caught by |
|---|---|
| push bonus removed (`score -= 0`) | "an enemy standing on the objective is the focus…" |
| sit only when strictly ahead (`>` for `>=`) | "level on the hold count…", "the AI orders every idle unit…" |
| sit without the can-hit check | no test; measured instead, see below |

## Measured

`probe 1000 600` with gcc, Blue the script, Orange the AI. "Before" is the
AI of note 14 (objective rule in, AI unaware of it); "sit unless pushed"
is the first version of this note; "sit unless hit" is what shipped.
Timeouts were 0 or 1 in every cell.

| Opponent | before | sit unless pushed | sit unless hit |
|---|---|---|---|
| random | 993-7 | 987-13, 284 by holding | **998-2, 133 by holding** |
| itself | 477-515 | 506-480 | 483-486 |
| focus fire | 770-220 | 677-319 | **800-194, 14 by holding** |
| hold spawn | 890-106 | 1000-0, all by holding | **1000-0, 994 by holding** |
| archer kites | 552-427 | 496-493 | 486-481, 34 by holding |

The first version sat while being shot at, and the two scripts that shoot
first took 90 and 50 matches off it. Waiting until someone can actually
hit us gets focus fire back above where it was: the side now meets it
standing together on the objective instead of strung out walking across
the map. The kiting Archer is the one real loss, about 60 matches: against
a script whose melee attacks the nearest unit while its Archer keeps three
tiles away, walking to the middle first costs tempo that the old
straight-at-them AI did not pay. That is the next thing to look at, with a
rule and a number, not by taking the objective back out.

`arena 1000 600`: 998 / 2 / 0, 133 won on the objective, mean length 108
ticks (note 14: 993 / 7 / 0, none on the objective, 103). gcc and
WebAssembly agree on `replay 1000 600`, 1000 of 1000 hashes.

## Viewer

The four tiles get an outline and a tint, gold when empty or contested and
the holding side's colour while one side stands there alone. Under the
tick bar, two thin bars fill towards `kHoldToWin`, Blue over Orange. A
match now ends when `winner()` says so, and the result band says "ORANGE
HOLDS THE OBJECTIVE" when that is how it ended. The hold is a count of
ticks, so the bar never goes back; it is the same number that is in the
hash.

## Probe

`tools/probe.cpp` is the five-opponent table from note 11, rebuilt: the
original was a scratch file and did not survive, which is the argument for
it being a tool in the tree. The scripts are deliberately short (the focus
fire one is "everyone attacks their Archer, then the most wounded") and
deterministic; only "random" draws from the seed.

**Questions this answers in an interview**
- How do you make an AI play a control point without it becoming passive?
  (Sit only while nobody can hit you; the fight rules take over the moment
  someone can.)
- How do you assign three units to four tiles deterministically? (Keep what
  you stand on, then nearest free tile in id order; no RNG, same answer on
  every machine.)
- What did the first version cost, and how did you find out? (90 matches
  against focus fire; the five-opponent table, not the random arena, showed it.)
