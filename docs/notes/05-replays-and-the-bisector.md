# 05 — Replays and the desync bisector

**What:** a replay file (seed, every tick's commands, the state hash after
every tick) and the `lockstep` tool: `record`, `verify`, `dump`. When two
machines disagree, `verify` names the first tick whose hash differs, and
diffing the two `dump`s of that tick names the field.

**One field list for everything.** `visit_fields()` (`core/include/lockstep/fields.hpp`)
walks every field of the state in a fixed order. `hash()`, `dump()` and
`first_difference()` all use it. Before this, the hash had its own list;
a desync tool with a second list could miss exactly the field the hash
caught, and nothing would notice. Now there is nothing to keep in sync.
Every value is hashed as 8 little-endian bytes, signed ones as their 32-bit
pattern, so the hash does not depend on the host's integer sizes.

**Per-tick hashes make the search linear and cheap.** With only a final hash,
finding where two runs split would need a binary search that re-simulates
from the start at each probe. Storing the hash after every tick (8 bytes a
tick, 600 ticks in a ~16 kB text file) turns it into a scan: the first index
where the two lists differ *is* the tick. Comparing the two states at that
tick gives the field, because the state before it still matched.

**Measured: 100 out of 100 injected desyncs located exactly.** The test
records a 400-tick replay, then 100 times corrupts one field right after one
random tick (a map tile, a unit's hp, x, y, cooldown or target, or the RNG)
and asks `locate()` where the run went wrong. All 100 come back with the right
tick and the right field. Reporting the tick one too early, a classic
off-by-one, fails 201 assertions (checked once by hand).

**Across machines.** A replay recorded by the gcc build verifies tick by tick
under MSVC and under WebAssembly. CI now does the same on every push: the MSVC
job records one, the WebAssembly job plays it and fails on the first differing
tick. Editing one hash in the file by hand makes `verify` report
`desync at tick 125` and exit with 1.

**The text format is strict.** `from_text` rejects anything malformed,
including a truncated file (it requires the closing `end` line), rather than
returning half a replay. A half-read replay would show up as a desync at the
cut, which would send someone hunting a bug that is not there.

**Questions this answers in an interview**
- How do you find the first tick where two simulations diverge, and why store
  a hash per tick rather than only at the end?
- How do you keep a hash and a debug dump from drifting apart?
- How do you test a debugging tool? (Inject known bugs and check it names them.)
