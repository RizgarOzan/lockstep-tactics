# lockstep-tactics

[![determinism](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml/badge.svg)](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml)

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
> push). Not yet: replay files, the desync bisector, rendering.

## What makes it deterministic

Each rule has a short note on why it exists. Five of them (fixed-point
rounding, the PCG32 output, the command sort, simultaneous damage, hash
coverage) were broken on purpose once to confirm a test catches it.

| Rule | Why | Note |
|---|---|---|
| Q16.16 fixed-point, never `float` | compilers may fuse or reorder float maths | [01](docs/notes/01-fixed-point.md) |
| Own PCG32 and bounded draw, not `<random>` | `std::*_distribution` differs between STLs | [02](docs/notes/02-rng.md) |
| Units in a vector sorted by id | `unordered_map` iteration order differs | [03](docs/notes/03-map-and-units.md) |
| One random draw per tile in map generation | RNG position must not depend on outcomes | [03](docs/notes/03-map-and-units.md) |
| Commands sorted on every field | arrival order differs between machines | [04](docs/notes/04-commands-and-combat.md) |
| Damage applied after everyone acts | unit ids must not decide who dies | [04](docs/notes/04-commands-and-combat.md) |
| The hash test edits each field in turn | a field missing from the hash is an invisible desync | [03](docs/notes/03-map-and-units.md) |

## How the check works

```
tools/replay.cpp  ── seed → random orders → step() × 600 → hash()   (one line per seed)
        │
        ├── windows-latest, MSVC x64   → msvc.txt
        └── ubuntu-latest, Emscripten  → wasm.txt (run under Node)
                                            │
                              compare job: diff msvc.txt wasm.txt
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

## Roadmap

1. ~~One map, three unit types, fixed-point movement and combat, fixed timestep.~~ Done.
2. Replay files: record inputs, play them back, check the hash every tick.
3. A desync bisector that names the first differing tick and field.
4. Deliberately risky code paths (an unordered container, float interpolation
   on the render side) to prove the checks catch what they claim to.
5. raylib rendering, playable in the browser.

## Licence

MIT — see [LICENSE](LICENSE).
