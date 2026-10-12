# It worked on MSVC: the desync that only two of three compilers show

*Rızgar Ozan · 2026-10-12 · [Türkçe](2026-10-12-worked-on-msvc-tr.md)*

[lockstep-tactics](https://github.com/RizgarOzan/lockstep-tactics) is a small
C++20 tactics simulation with one promise: the same inputs give the same state
hash on every compiler, every tick. That promise is what lockstep multiplayer
and replays stand on, and it is easy to claim and hard to check. So the repo
checks it on every push: a thousand seeded matches are played under MSVC on
Windows and under WebAssembly on Linux, and the two lists of final hashes are
diffed.

The check passing a few hundred times told me nothing about whether it could
fail. Rules that never fail anywhere look like superstition. So I built the
simulation wrong on purpose, twice, and counted.

## Breaking rule one: units act in `unordered_map` order

The simulation keeps units in a `std::vector` sorted by id and lets them act
in that order. The broken build (`-DLOCKSTEP_RISKY=unordered`) puts them in a
`std::unordered_map<uint32_t, Unit*>` and iterates that instead. Turn order
decides who takes which random number for a hit roll, so a different order is
a different match.

Over 1,000 matches of 600 ticks, matches that end differently from the normal
build:

| Compiler | differing matches |
|---|---|
| MSVC 19.50 x64 | **0** |
| gcc 16 (libstdc++) | 881 |
| Emscripten 6 / WebAssembly (libc++) | 860 |

gcc and WebAssembly also disagree with *each other* in 249 matches.

The zero is the interesting number. MSVC's `unordered_map` keeps its elements
in a list in insertion order, and with six small keys and no rehash, iterating
it happens to come out in id order, which is exactly the safe order. The bug
is invisible on the compiler most Windows developers test on. Every unit test
passes. Every local replay matches. Ship it, and the first Linux or browser
client desyncs in 86% of matches.

That is the whole argument for checking across compilers rather than across
runs: "it works on my machine" was literally true here.

## Breaking rule two: movement in `float`

The simulation uses Q16.16 fixed-point everywhere, because note 01 of the
repo says compilers may fuse or reorder float maths. The second broken build
(`-DLOCKSTEP_RISKY=float`) moves units along the straight line to their
target with `float`, a square root and a division, then converts back.

| | differing matches |
|---|---|
| MSVC vs gcc vs WebAssembly, any pair | **0** / 1000 |

I expected this one to break and it did not. All three targets use IEEE
scalar floats with the same rounding, `sqrt` is correctly rounded, and none of
them contracts `a*b + c` into a fused multiply-add in ISO C++ mode. On these
three compilers, today, float was deterministic.

Then one flag:

| | differing matches |
|---|---|
| gcc with `-mfma -ffp-contract=fast` vs plain gcc, float build | **12** / 1000 |
| the same flags, fixed-point build | 0 / 1000 |

With contraction allowed, the object file gains three FMA instructions (zero
without the flag), and twelve matches end differently. The fixed-point build
compiled with the same flags does not move at all: there is no float
arithmetic for the flag to touch. So the "no float" rule is not about these
compilers. It is about the build flag, the compiler upgrade or the ARM target
that has not been added yet, and the fixed-point rule is what makes those
non-events.

## What I take from it

- A determinism check that has never failed is unproven. Break the code on
  purpose and count what the check catches; the repo now ships both broken
  builds behind a CMake option so anyone can repeat the numbers.
- "Works on MSVC" and "works on gcc" are different claims. The container
  bug was invisible on one and loud on the other two.
- Float is not non-deterministic by nature. It becomes so the moment two
  builds differ in contraction, precision or library functions, and you do
  not control when that moment comes. Fixed-point removes the question.

Everything above is reproducible from the repo: `cmake -S . -B build-risky
-DLOCKSTEP_RISKY=unordered` (or `float`), `replay 1000 600`, diff against the
normal build. The reasoning behind each rule is in
[`docs/notes/`](https://github.com/RizgarOzan/lockstep-tactics/tree/main/docs/notes),
and note 06 is the full version of this post.
