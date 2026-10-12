# lockstep-tactics

[![determinism](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml/badge.svg)](https://github.com/RizgarOzan/lockstep-tactics/actions/workflows/determinism.yml)

A small turn-based tactics simulation in C++20, written to be **bit-identical
across compilers**: the same inputs must give the same state hash under MSVC
x64 and under WebAssembly, on every tick. That property is what lockstep
multiplayer and replays stand on, and it breaks in quiet ways — floats,
unordered containers, `std::sort` on equal keys, `size_t` width, signed
overflow.

> **Status: scaffold.** The determinism harness works end to end: CI builds
> both targets, plays 1,000 seeded replays of 600 ticks on each and diffs the
> final hashes. The simulation itself is still a placeholder; the map, three
> unit types and fixed-point movement and combat come next.

## How the check works

```
tools/replay.cpp  ── seed → inputs → step() × 600 → hash()   (one line per seed)
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

1. One map, three unit types, fixed-point movement and combat, fixed timestep.
2. Replay files: record inputs, play them back, check the hash every tick.
3. A desync bisector that names the first differing tick and field.
4. Deliberately risky code paths (an unordered container, float interpolation
   on the render side) to prove the checks catch what they claim to.
5. raylib rendering, playable in the browser.

## Licence

MIT — see [LICENSE](LICENSE).
