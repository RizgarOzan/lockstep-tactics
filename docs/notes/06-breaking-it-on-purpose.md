# 06 — Breaking it on purpose

**What:** a CMake option that builds three deliberately non-deterministic
variants, and what they did on the three compilers on this machine (MSVC
19.50 x64, gcc 16.2 x64, Emscripten 6 / WebAssembly under Node). Every number
is from 1,000 seeded matches of 600 ticks with `replay 1000 600`.

```bash
cmake -S . -B build-risky -DLOCKSTEP_RISKY=unordered   # or float, or size_t
```

The normal build is untouched by the option: its 1,000 hashes are identical
to the build without it.

## `unordered`: units act in hash-map order

Units take their turn in the iteration order of a
`std::unordered_map<uint32_t, Unit*>` instead of by id. Turn order decides who
uses which random number for a hit roll, so a different order is a different
match.

| | matches that differ from the normal build |
|---|---|
| MSVC | 0 / 1000 |
| gcc (libstdc++) | 995 / 1000 |
| Emscripten (libc++) | 996 / 1000 |

gcc and Emscripten also differ from each other in 299 matches. (First
measured on the simpler simulation as 881, 860 and 249; the numbers above
are from after note 11, when units path, block and need line of sight, so
a different turn order changes more.)

The surprise is MSVC: its `unordered_map` keeps elements in a list in
insertion order, and with six small keys and no rehash, iteration comes out
in id order, which is exactly the safe order. So the bug is invisible on the
compiler most Windows developers test on, and shows up on the other two.
The CI pair (MSVC vs WebAssembly) would have flagged 860 of 1,000 matches.
"It works on my machine" is the failure mode this project exists to catch.

## `float`: movement in float with a square root

`walk()` moves along the straight line to the target with `float`, a
`sqrt` and a division, and converts back to fixed-point.

| | matches that differ |
|---|---|
| MSVC vs gcc vs Emscripten | 0 / 1000 for every pair |
| gcc with `-mfma -ffp-contract=fast` vs plain gcc | **4 / 1000** (12 before note 11) |
| *fixed-point build* with the same flags vs normal | 0 / 1000 |

On these three targets, float was deterministic. That is expected and not a
contradiction of note 01: all three use SSE2 or WebAssembly scalar floats
with IEEE rounding, `sqrt` is correctly rounded, and none of them contracts
`a*b + c` by default (MSVC's `/fp:precise`, and ISO `-std=c++20` mode in gcc
and clang, keep `-ffp-contract` off).

One flag changes that. With `-mfma -ffp-contract=fast` gcc is allowed to fuse
multiply and add into one instruction with a single rounding; the object file
then contains FMA instructions (none without the flag), and 4 of 1,000
matches end differently (12 on the simpler simulation; paths now decide
most of a walk, so the float stride has less to get wrong). The fixed-point build, compiled with the same flags,
does not move: there is no float arithmetic for the flag to change. So the
rule in note 01 is not about these three compilers today; it is about the
build flag, the compiler upgrade or the ARM target that has not been added
yet.

## `size_t`: the unit count hashed at the target's width

`hash()` also feeds `units.size()` into the hash as a `size_t`, one byte per
byte of *this target's* `size_t`: 8 bytes on x64, 4 on wasm32. The hash
comment in `hash.hpp` says never to do this; this build does it.

| | matches that differ |
|---|---|
| MSVC vs gcc | 0 / 1000 (both 64-bit) |
| MSVC vs Emscripten | **1000 / 1000** |
| gcc vs Emscripten | **1000 / 1000** |

A replay recorded under MSVC fails `lockstep verify` under WebAssembly at
tick 1, and passes under gcc. So the whole class of bug is invisible to a
team that tests on two 64-bit desktops, and the first 32-bit target (a
browser, a phone, a console) finds it on the very first tick. It is also the
easy case for the bisector: the field it names is the hash itself, not the
state, because every field of the state is in fact identical.

**What this note is for.** Rules that never fail anywhere look like
superstition. This puts a number next to two of them, including the case
where the rule turned out *not* to matter on the current targets.

**Questions this answers in an interview**
- Can an `unordered_map` that passes all your tests still desync? (Yes: on
  MSVC it iterated in insertion order here, on libstdc++ and libc++ it did not.)
- Is float maths non-deterministic? (Not inherently; it becomes so when
  compilers or flags differ in contraction, precision or library functions.)
- How do you show a determinism check works? (Build a version that breaks it
  and count what the check catches.)
- Why does the CI need a 32-bit target at all? (A `size_t` in the hash, or
  in any state, agrees between every 64-bit compiler and disagrees with
  wasm32 on tick 1.)
