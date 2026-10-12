# lockstep-tactics

[![determinism](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml/badge.svg)](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml)

**[▶ Watch a match in the browser](https://rizgarozan.github.io/lockstep-tactics/)**: the
simulation compiled to WebAssembly, with the live state hash on screen.

<p align="center">
  <img src="docs/media/match.gif" width="420" alt="Seed 1: blue (circle soldier, triangle archer, square knight) and orange units walk, trade hits with flashes and attack lines, and blue wins at tick 139; the state hash updates in the corner every tick">
</p>
<p align="center"><sub>Seed 1 at 2× speed, rendered headless by <code>viewer --capture</code>.</sub></p>

A small tick-based tactics simulation in C++20, written to be **bit-identical
across compilers**: the same inputs must give the same state hash under MSVC
x64 and under WebAssembly, on every tick. That property is what lockstep
multiplayer and replays stand on, and it breaks in quiet ways — floats,
unordered containers, `std::sort` on equal keys, `size_t` width, signed
overflow.

> **Status: the simulation plays.** Two players, a 16×16 map, a Soldier, an
> Archer and a Knight each; move and attack orders, hit rolls, cooldowns,
> simultaneous damage. In 1,000 random matches of 600 ticks, MSVC x64 and
> WebAssembly reach the same final hash in every one (CI checks this on each
> push). A replay recorded under MSVC also verifies tick by tick under
> WebAssembly, and when two runs disagree the `lockstep` tool names the first
> tick and the field: it located 100 of 100 injected desyncs exactly.
> A raylib viewer plays matches natively and [in the browser](https://rizgarozan.github.io/lockstep-tactics/).

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

Two of these rules were also broken in a whole build (`-DLOCKSTEP_RISKY=unordered|float`,
[note 06](docs/notes/06-breaking-it-on-purpose.md)), 1,000 matches each:

| Broken build | MSVC | gcc | WebAssembly |
|---|---|---|---|
| units act in `unordered_map` order | 0 differ | 881 differ | 860 differ |
| movement in `float` | same on all three | | |
| `float` + gcc `-mfma -ffp-contract=fast` | | 12 differ | |

MSVC's `unordered_map` happened to iterate in id order here, so that bug only
shows on the other two compilers. Float was deterministic on all three targets
until one compiler flag let gcc fuse multiply-adds; the fixed-point build
under the same flag did not change.

## Why not …

- **… just use `float` and test on one machine?** That is what most hobby
  lockstep games do, and it is exactly the bug this repo measures: the
  `unordered_map` build passes every test on MSVC and desyncs on two other
  compilers. The point is not that fixed-point is clever; it is that the
  claim "bit-identical" is checked on every push, across compilers, with
  replays that any machine can verify.
- **… an engine's netcode (Unity Netcode, Godot, Photon)?** Those give you
  state synchronisation; they do not give you a simulation that is the same
  everywhere, and they cannot be dropped into a browser tab. This is the
  layer underneath, small enough to read in an afternoon (about 700 lines of
  C++ for the simulation and the tools).
- **… GGPO or a rollback library?** Rollback assumes determinism and solves
  latency on top of it. The hard part they leave to you, "is my simulation
  actually deterministic, and where did it stop being so", is what the replay
  format and the bisector here answer.

## How the check works

```
tools/replay.cpp  ── seed → random orders → step() × 600 → hash()   (one line per seed)
        │
        ├── windows-latest, MSVC x64   → msvc.txt
        └── ubuntu-latest, Emscripten  → wasm.txt (run under Node)
                                            │
                              compare job: diff msvc.txt wasm.txt

tools/lockstep.cpp ── MSVC job: record a 600-tick replay (commands + hash per tick)
                      wasm job: verify it, failing on the first tick that differs
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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build
build/replay 1000 600
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

## Roadmap

1. ~~One map, three unit types, fixed-point movement and combat, fixed timestep.~~ Done.
2. ~~Replay files: record inputs, play them back, check the hash every tick.~~ Done.
3. ~~A desync bisector that names the first differing tick and field.~~ Done.
4. ~~Deliberately risky code paths to prove the checks catch what they claim to.~~
   Done for the simulation ([note 06](docs/notes/06-breaking-it-on-purpose.md)); render-side
   float interpolation comes with rendering.
5. ~~raylib rendering, playable in the browser.~~ Done ([note 07](docs/notes/07-viewer.md)).
6. ~~Touch controls for the browser page~~ (the HUD buttons take a finger as well as a mouse) and a real player instead of random orders.

## Contributing

Good first changes, each small enough for one pull request:

- a new deliberately broken build under `LOCKSTEP_RISKY` (a `std::sort` on
  equal keys, a `size_t` in the hash, signed overflow) with its measured
  numbers added to the table above;
- a third CI compiler (clang on macOS) in `determinism.yml`;
- a real player in the browser viewer: pick a unit, click a tile or an enemy,
  and send a `Command`; replays must keep working unchanged.

The rule for every change: `ctest` green under at least two compilers, and
`replay 1000 600` identical between them. If a change is meant to alter the
simulation, say so in the commit and expect every replay hash to move.

## Licence

MIT — see [LICENSE](LICENSE).
