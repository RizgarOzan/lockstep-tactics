# 14 — An objective in the middle of the map

**What:** the first half of roadmap item 13. The 2x2 tiles in the middle
of the map are the objective. At the end of every tick, a side with units
on it while the other side has none there scores one tick of hold;
`kHoldToWin` (100) ticks win the match, as does being the last side with
units. The AI does not go for it yet, which is the second half.

## The rules, and where they sit in the tick

`step()` scores the objective as step 4, after the dead are removed, from
where the survivors stand. So a unit killed this tick does not contest the
objective this tick, and a unit that walked onto it does. Contested (both
sides there) and empty both score nobody. Held ticks are never reset: a
side that held for 60 ticks and was pushed off needs 40 more, not 100.

`winner()` reads the state and changes nothing: 0 or 1 for the side that
won, 2 when both sides lost their last unit on the same tick, -1 while the
match goes on. The arena tool now plays until `winner()` says so, and
counts how many matches were won on the objective.

`held[0]` and `held[1]` are in `visit_fields()`, so they are in the hash,
the dump and the bisector's diff. The hash coverage test edits each in
turn.

## Map generation

The four objective tiles are set to floor after the random walls are
drawn, before the pocket fill. Setting tiles after the draws does not move
the RNG: every interior tile still costs exactly one draw (note 03).

Could the objective end up in a pocket that the fill then walls over? It
needs a ring of at least eight walls around it. I wrote a corridor that
opened the objective's row whenever the fill missed it, then counted how
often it ran: 0 of 100,000 seeds. Code that never runs cannot be tested
and is not worth keeping, so it is gone. If a seed ever does wall the
objective in, the fill walls it over too and that map is won by fighting
alone. `tests/test_objective.cpp` checks seeds 0 to 999: the objective is
floor and both spawns can walk to it.

## Tests, and what broke them on purpose

| Change | Caught by |
|---|---|
| a contested objective counts for whoever is there | "a contested objective holds for nobody" |
| the four tiles are not forced to floor | "on every map the objective is floor…" (seed 2) |
| `held[1]` left out of `visit_fields()` | "the hash covers every field of the state" |

## Measured

`arena 1000 600` with gcc: AI 993, random 7, draw 0, **0 won on the
objective**, mean length 103 ticks. Before the change it was 995 / 5 / 0
and 106 ticks; the maps changed (four tiles forced to floor), so every
replay hash moved, and the AI's record moved with the maps. gcc and
WebAssembly still agree: `replay 1000 600`, 1000 of 1000 identical.

Zero wins on the objective is the honest result. Matches end by
elimination in about 103 ticks, `kHoldToWin` is 100, and neither the random
player nor the AI walks to the middle on purpose. The rule exists; nobody
plays for it yet. Next: an AI that contests the objective when it is held
against it and holds it when it is ahead, measured against the five
opponents in note 11.

**Questions this answers in an interview**
- Where in a tick do you score a control point, and why there? (After
  damage and removal, so the dead don't count and the order of units
  inside the tick doesn't matter.)
- How do you add a rule to map generation without breaking determinism?
  (Don't add or skip random draws; overwrite tiles after drawing.)
- When do you delete code that guarantees an invariant? (When you measured
  that it never runs, so it can't be tested, and the failure it guards
  against is not a crash.)
