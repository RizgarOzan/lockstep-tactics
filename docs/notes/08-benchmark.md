# 08 — How fast is a tick, and the three allocations that were most of it

**What:** a `bench` tool that times `step()` alone, and the one change it
led to: the per-tick heap allocations are gone. Every number is from this
machine (AMD Ryzen 9 270 laptop, on mains power, Windows 11), 200 seeded
matches × 600 ticks, 50 repeats per run, three runs per cell. The table
shows the median of the middle run and, in brackets, the lowest minimum.

| Build | Before | After | |
|---|---|---|---|
| gcc 16.2 `-O3` | 187 ns/tick (162) | 66 ns/tick (60) | 2.8× |
| MSVC 19.50 `/O2` | 225 ns/tick (201) | 88 ns/tick (80) | 2.6× |
| Emscripten 6, Node 24 | 141 ns/tick (133) | 78 ns/tick (72) | 1.8× |

The simulation did not change: `replay 1000 600` prints the same 1,000
hashes before and after on all three compilers, and `bench` folds every
final state into one number that is the same in every row above.

## What the benchmark times, and what it leaves out

`replay` was the obvious thing to time and the wrong one. Its loop calls
`random_orders()` before every `step()`, and `random_orders` builds three
vectors a tick to pick a unit and a target. That is test input, not the
simulation; a real game gets its orders from the network. So `bench` plays
each match once to record the orders, then times a loop that only calls
`step()` on the recorded orders. `hash()` is called once per match, outside
the per-tick cost, because the hash is a check, not the simulation either.

The clock is `std::chrono::steady_clock` around a whole run of 200 matches,
not around each tick: a tick is well under a microsecond and the clock
itself costs tens of nanoseconds. 50 repeats, then min, median and max.
On a laptop the max moves by 30–50% between runs and the median by about
10%; the min is the steadiest number, which is why the table carries both.

Nothing here runs in CI. Shared runners share their CPU, and a benchmark
whose numbers swing with the neighbours would fail builds for no reason.
The determinism check stays the gate; speed is measured by hand and written
down.

## Where the time went

`gprof` under MinGW produced an empty profile (the sampling timer never
fires in this build), so the hunt was by reading: `step()` created three
`std::vector`s on every tick — a sorted copy of the commands, a list of
pointers to the acting units, and the hits to apply. With six units and
one or two orders a tick, the work between the allocations is a few dozen
integer operations. Three `malloc`/`free` pairs were most of a tick.

The fix keeps the same three stages:

- the orders are sorted in a `std::array<Command, 8>` on the stack, and
  only a tick with more than eight orders falls back to a vector;
- the units are walked in place: nothing is added to or removed from the
  vector until the damage stage, so the pointer list was a copy of the
  iteration order it already had;
- the hits go in a `std::array<Hit, kMaxUnits>`, because a unit lands at
  most one hit per tick and `make_world()` never spawns more than six.

The risky `unordered` build still needs its pointer list, since its whole
point is to iterate a hash map; it keeps a vector under the `#if`.

## The WebAssembly build was the fast one

Before the change WebAssembly ran a tick in 141 ns against 187 for gcc and
225 for MSVC, which is the opposite of what a bytecode target should do.
The explanation is the allocator: Emscripten's single-threaded `dlmalloc`
is a few loads and stores, while the Windows CRT heap that gcc and MSVC
link takes a lock and goes through the heap manager on every call. Once
the loop stopped allocating, the three builds landed within 30 ns of each
other, native ahead. A benchmark that includes allocation measures the
allocator as much as the code.

## How to run it

```bash
cmake --build build --target bench
build/bench 200 600 50        # seeds, ticks, repeats
node build-wasm/bench.js 200 600 50
```

`bench.js` loads `bench.wasm` from its own directory, so copy both when
keeping an old build to compare against.
