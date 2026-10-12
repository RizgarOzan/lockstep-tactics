# 08 — A real player

**What:** `core/include/lockstep/player.hpp`, one pure function that turns a
click on the board into a `Command`, and the viewer's play mode that uses it.
Press *play Blue* (or P), click one of your units or its card, then a tile to
move or an orange unit to attack. Orange is the AI from note 09.

**The click is not part of the simulation.** `click(world, player, selected,
tx, ty)` reads the `World` and returns a new selection and maybe a `Command`.
It never writes anything. The viewer queues the command and hands it to
`step()` on the next tick together with the AI's orders, exactly as
`random_orders()` did before. So a played match is still a seed plus a list
of commands per tick, and the replay format did not change by a byte: a
match you played in the browser, saved with the *save* button, verifies under
MSVC with `lockstep verify`.

**Orders apply on the next tick, not now.** A click between two ticks has to
wait for the tick boundary, because the state only changes in `step()`. At
ten ticks a second that is at most 100 ms, which nobody notices; it is also
what a networked lockstep game has to do anyway, where the delay is the
round trip. Pausing the match and giving orders works for the same reason:
the orders sit in the queue until the next tick runs.

**The newest order for a unit wins, before sorting.** Two orders for one unit
in one tick are sorted by every field inside `step()` (note 04), which is
deterministic but not what the player meant. The viewer therefore replaces a
waiting order for the same unit before the tick, so the last click is the
one that lands and the sort never sees two.

**Tested without a window.** `tests/test_player.cpp` checks `unit_at()` and
`click()` on known positions (own unit selects, enemy attacks, floor moves,
wall and outside do nothing) and records a 300-tick match where Blue is
driven by clicks and Orange by the random player, then plays it back and
round-trips it through the text format. The viewer only adds the mouse.

**Measured in the browser.** The three findings from note 07 still hold with
a game under the buttons. One more: the browser automation's synthetic
`mousedown`/`mouseup` never reached GLFW even when held, while a real press
and drag of a couple of pixels did, so the manual test was done with drags.
Selecting the Soldier, ordering it to a tile, resuming and watching it walk
there while the AI killed the Knight: all as designed, including the
"fallen" card.

**Questions this answers in an interview**
- How does player input enter a lockstep simulation? (As a command queued
  for the next tick, never as a direct state change.)
- Why can a human game and an AI game share one replay format?
- Where does "last order wins" get decided, and why not in `step()`?
