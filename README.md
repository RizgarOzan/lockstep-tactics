# lockstep-tactics

[![determinism](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml/badge.svg)](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml)
[![licence: MIT](https://img.shields.io/badge/licence-MIT-blue)](LICENSE)

A C++20 tactics game that stays bit-identical on MSVC x64, Apple clang arm64
and WebAssembly, checked on every push.
**[Play it in the browser](https://rizgarozan.github.io/lockstep-tactics/).**

<p align="center">
  <img src="docs/media/match.gif" width="720" alt="Seed 1: random Blue against the AI. Orange advances together, walks round the walls, focuses one blue unit at a time while its Archer shoots and steps back; the unit cards on the right show health and what each unit is doing, and Orange wins at tick 118; the state hash in the top right updates every tick">
</p>
<p align="center"><sub>Seed 1 at 2× speed, random Blue against the AI, rendered headless by <code>viewer --capture</code>.</sub></p>

Lockstep multiplayer and replays only work if every machine computes the same
state from the same inputs, and that breaks in quiet ways: floats, unordered
containers, `std::sort` on equal keys, `size_t` width, signed overflow. This
project removes each of them and checks the result with a state hash on
every tick. In the browser you can watch the AI, play Blue against it, or
play a friend in another browser over WebRTC, and save a replay any machine
can verify.

## Results

| Check | Result | Where |
|---|---|---|
| Same final hash on MSVC x64, Apple clang arm64, WebAssembly | 1,000 of 1,000 seeded matches, 600 ticks each | CI, [`determinism.yml`](.github/workflows/determinism.yml) |
| Bisector names the first differing tick and field | 100 of 100 injected desyncs | [`tests/test_replay.cpp`](tests/test_replay.cpp), [note 05](docs/notes/05-replays-and-the-bisector.md) |
| Integer-only AI against random orders | 998 wins, 2 losses, 0 draws in 1,000 matches | `build/arena 1000 600` |
| Cost of one tick | 64–89 ns before pathfinding, about 1.0 µs with A* (gcc) | `build/bench`, one laptop, [note 12](docs/notes/12-benchmark.md) |

## Quick start

Needs CMake 3.25+, Ninja and a C++20 compiler.

```bash
git clone https://github.com/RizgarOzan/lockstep-tactics && cd lockstep-tactics
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build && ctest --test-dir build
```

Contents: [What makes it deterministic](#what-makes-it-deterministic) ·
[Why not …](#why-not-) · [How fast](#how-fast) ·
[How the check works](#how-the-check-works) · [Build](#build) ·
[Repository layout](#repository-layout) · [Limits](#limits) · [Contributing](#contributing)

## What makes it deterministic

Each rule has a short note on why it exists. Six of them (fixed-point
rounding, the PCG32 output, the command sort, simultaneous damage, hash
coverage, the bisector's tick) were broken on purpose once to confirm a test
catches it.

| Rule | Why | Note |
|---|---|---|
| Q16.16 fixed-point, never `float` | compilers may fuse or reorder float maths | [01](docs/notes/01-fixed-point.md) |
| Own PCG32 and bounded draw, not `<random>` | `std::*_distribution` differs between STLs | [02](docs/notes/02-rng.md) |
| Units in a vector sorted by id | `unordered_map` iteration order differs | [03](docs/notes/03-map-and-units.md) |
| One random draw per tile in map generation | RNG position must not depend on outcomes | [03](docs/notes/03-map-and-units.md) |
| Commands sorted on every field | arrival order differs between machines | [04](docs/notes/04-commands-and-combat.md) |
| Damage applied after everyone acts | unit ids must not decide who dies | [04](docs/notes/04-commands-and-combat.md) |
| The hash test edits each field in turn | a field missing from the hash is an invisible desync | [03](docs/notes/03-map-and-units.md) |
| Hash, dump and diff walk one field list | a debug tool that reads other fields than the hash misses desyncs | [05](docs/notes/05-replays-and-the-bisector.md) |
| Float only where nothing reads it back (rendering) | smooth motion without touching the simulated state | [07](docs/notes/07-viewer.md) |
| A click becomes a `Command` for the next tick | input enters the state only through `step()` | [08](docs/notes/08-player.md) |
| The AI uses integers only and emits only changes | its commands are in the replay too | [09](docs/notes/09-ai-and-matchups.md) |
| Online, a tick runs when both sides' orders for it are in | orders scheduled three ticks ahead; frames carry the last hash | [10](docs/notes/10-two-browsers.md) |
| Paths by BFS with a fixed tie order; cover by integer Bresenham | the same tile and the same shot on every machine | [11](docs/notes/11-paths-cover-and-bodies.md) |
| Pockets filled by a flood fill that draws no random numbers | map generation keeps one draw per tile | [11](docs/notes/11-paths-cover-and-bodies.md) |
| Every unit decides from the tick's starting positions | acting first within a tick buys nothing | [13](docs/notes/13-fair-fast-and-rollback.md) |
| Rollback confirms a tick only with both sides' orders | the replay holds confirmed ticks, never guesses | [13](docs/notes/13-fair-fast-and-rollback.md) |
| On the walk to the objective, wait for the group only with an enemy within eight tiles | without the gate the side waits for itself across an empty map and times out | [16](docs/notes/16-walking-together.md) |

Three of these rules were also broken in a whole build (`-DLOCKSTEP_RISKY=unordered|float|size_t`,
[note 06](docs/notes/06-breaking-it-on-purpose.md)), 1,000 matches each:

| Broken build | MSVC | gcc | WebAssembly |
|---|---|---|---|
| units act in `unordered_map` order | 0 differ | 995 differ | 996 differ |
| movement in `float` | same on all three | | |
| `float` + gcc `-mfma -ffp-contract=fast` | | 4 differ | |
| a `size_t` in the state hash | same as gcc | | 1000 differ, from tick 1 |

(Re-measured after the simulation changes in note 11; the first round, on the
simpler simulation, read 881, 860 and 12.)

MSVC's `unordered_map` happened to iterate in id order here, so that bug only
shows on the other two compilers. Float was deterministic on all three targets
until one compiler flag let gcc fuse multiply-adds; the fixed-point build
under the same flag did not change. A `size_t` in the hash is invisible
between two 64-bit compilers and breaks every single tick against wasm32.

Write-up of these two experiments:
[English](docs/blog/2026-10-12-worked-on-msvc-en.md) ·
[Türkçe](docs/blog/2026-10-12-worked-on-msvc-tr.md).

## Why not …

- **… just use `float` and test on one machine?** That is what most hobby
  lockstep games do, and it is exactly the bug this repo measures: the
  `unordered_map` build passes every test on MSVC and desyncs on two other
  compilers. The point is not that fixed-point is clever; it is that the
  claim "bit-identical" is checked on every push, across compilers, with
  replays that any machine can verify.
- **… a smarter AI?** The one here (note 09) is a few integer rules: one
  focus for the side, fight what is in reach, wait for the slow teammate,
  Archers shoot and step back. Note 11 shows what measuring it against
  scripted opponents found and fixed, most of it in the simulation rather
  than the AI (no paths, no cover, no bodies). It still has no look-ahead
  and no retreat; its job is to make the demo a game and to put a second
  order source into the replays.
- **… an engine's netcode (Unity Netcode, Godot, Photon)?** Those give you
  state synchronisation; they do not give you a simulation that is the same
  everywhere, and they cannot be dropped into a browser tab. This is the
  layer underneath, small enough to read in a day (about 2,000 lines of
  C++ in `core/`, headers included).
- **… GGPO or a rollback library?** Rollback assumes determinism and solves
  latency on top of it. The hard part they leave to you, "is my simulation
  actually deterministic, and where did it stop being so", is what the replay
  format and the bisector here answer.

## How fast

`bench` times `step()` on recorded orders and leaves the input generator
out. Before pathfinding a tick cost 64–89 ns on this laptop across gcc,
MSVC and WebAssembly, after the three per-tick heap allocations were
removed (it was 1.8–2.7× slower before that, and until then WebAssembly
was the fastest build, most likely because its allocator is single-threaded).
The shortest-path walk in [note 11](docs/notes/11-paths-cover-and-bodies.md)
is A* with a bucketed open list, run per moving unit whose straight line
is blocked, and it dominates: about 1.0 µs per tick under gcc (1.85 with
the first breadth-first version), 600 ticks in well under a millisecond,
fine for ten ticks a second and still the obvious next thing to optimise.
Method, numbers and the fix: [note 12](docs/notes/12-benchmark.md). The
benchmark does not run in CI on purpose; shared runners are not a clock.

## How the check works

```
tools/replay.cpp  ── seed → random Blue vs AI Orange → step() × 600 → hash()   (one line per seed)
        │
        ├── windows-latest, MSVC x64          → msvc.txt
        ├── macos-latest, Apple clang arm64   → clang.txt
        └── ubuntu-latest, Emscripten         → wasm.txt (run under Node)
                                                   │
                              compare job: diff msvc.txt clang.txt; diff msvc.txt wasm.txt

tools/lockstep.cpp ── MSVC job: record a 600-tick replay (commands + hash per tick)
                      wasm job: verify it, failing on the first tick that differs
tools/arena.cpp    ── MSVC job: 1,000 matches, prints who won
```

Finding a desync by hand:

```bash
lockstep verify match.txt          # on the machine that disagrees: "desync at tick 125"
lockstep dump match.txt 125 > a.txt  # on both machines, then diff a.txt b.txt
```

The state hash is FNV-1a over fixed-width integers in little-endian byte
order (`core/include/lockstep/hash.hpp`), so the hash itself cannot differ
between a 64-bit and a 32-bit target.

## Build

Needs CMake ≥ 3.25 and a C++20 compiler; doctest is fetched by CMake.

```bash
git clone https://github.com/RizgarOzan/lockstep-tactics && cd lockstep-tactics
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
build/replay 1000 600     # one hash per seed, what CI diffs between compilers
build/arena 1000 600      # AI vs random: who won
build/probe 1000 600      # AI vs five scripted opponents, as a table
build/bench 200 600 50    # ns per tick: min, median, max over 50 repeats
```

What the first two print (gcc 16, Release; MSVC, clang and WebAssembly print the same hashes):

```
$ build/replay 3 600      # seed, state hash after 600 ticks, units left
0 26bff752087859d1 2
1 9131a69ff17a5fff 3
2 e1870a78387b20a6 3
$ build/arena 1000 600
1000 matches: AI (orange) 998, random (blue) 2, draw 0; 59 won on the objective; mean length 111 ticks
```

WebAssembly, with the Emscripten SDK active:

```bash
emcmake cmake -S . -B build-wasm -DCMAKE_BUILD_TYPE=Release
cmake --build build-wasm
node build-wasm/replay.js 1000 600
```

The viewer is opt-in because it fetches raylib:

```bash
cmake -S . -B build-viewer -DLOCKSTEP_VIEWER=ON && cmake --build build-viewer --target viewer
emcmake cmake -S . -B build-web -DLOCKSTEP_VIEWER=ON && cmake --build build-web --target viewer   # index.html
```

In the viewer: space pauses, N is the next seed, R restarts, up/down change
the speed, P switches between watching and playing Blue, S saves the replay
(a download in the browser). Playing: click a unit or its card, then a tile
or an enemy.

To play a friend, one browser presses *Create a room* and reads out the
six-letter code, the other types it and presses *Join*. Ticking *rollback*
before creating the room removes the input delay: orders run at once and
the ticks since are re-run when the other side's arrive
([note 13](docs/notes/13-fair-fast-and-rollback.md)). The handshake goes
through a public [ntfy.sh](https://ntfy.sh) topic (`?relay=` points the page
at another ntfy server); after that the two browsers exchange lockstep
frames directly over WebRTC ([note 10](docs/notes/10-two-browsers.md)). The
same `lockstep::Lockstep` class, with no sockets in it, is what
`tests/test_net.cpp` runs two of against each other.

## Repository layout

```
core/include, core/src   the simulation: world, replay, rollback, net, AI (no sockets, no float state)
tests/                   doctest suites, one file per area, run by ctest
tools/                   replay, arena, probe, bench, and the raylib viewer
docs/notes/              numbered notes, one per rule or step
docs/media/              the GIF and images used here
docs/blog/               write-ups
.github/workflows/       determinism.yml (hash diff across compilers), pages.yml (browser build)
```

## Limits

- Small on purpose: two players, one 16×16 map, three unit types, one
  objective. A fourth unit type is the next planned step.
- The AI is a handful of integer rules with no look-ahead and no retreat. It
  beats random orders, not a careful human.
- CI compares the final hash of 1,000 seeds on three targets and verifies one
  MSVC replay tick by tick in WebAssembly. Other compilers and platforms
  (gcc on Linux arm64, MSVC x86) are not in CI.
- The timings come from one laptop and are not run in CI; shared runners are
  not a stable clock.
- Online play signals through a public ntfy.sh topic and uses a public STUN
  server with no TURN fallback, so two browsers behind strict NATs may not
  connect.

Finished steps and what each one measured are in [`docs/notes`](docs/notes),
numbered in the order they were done.

## Contributing

Good first changes, each small enough for one pull request. The ones with
an issue already written, including what "done" means, are under
[`good first issue`](https://github.com/RizgarOzan/lockstep-tactics/labels/good%20first%20issue);
comment on one before you start.

- a new deliberately broken build under `LOCKSTEP_RISKY` (a `std::sort` on
  equal keys, signed overflow, a `double` in the AI's scoring) with its
  measured numbers added to the table above;
- a fourth CI target (gcc on Linux arm64, or MSVC x86) in `determinism.yml`;
- a stronger AI in `core/src/ai.cpp` that still uses integers only, for example a retreat
  when outnumbered; measure it with `build/probe 1000 600` against the five
  opponents ([note 16](docs/notes/16-walking-together.md) has the latest table);
- a fourth unit type, with its row in the matchup table and in the tests;
- a TURN fallback or a different signalling relay for the online mode, kept
  behind the same `?relay=` parameter.

The rule for every change: `ctest` green under at least two compilers, and
`replay 1000 600` identical between them. If a change is meant to alter the
simulation, say so in the commit and expect every replay hash to move.

## Licence

MIT, see [LICENSE](LICENSE).
