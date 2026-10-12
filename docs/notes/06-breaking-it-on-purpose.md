# 06 — Breaking it on purpose

**What:** a CMake option that builds two deliberately non-deterministic
variants, and what they did on the three compilers on this machine (MSVC
19.50 x64, gcc 16.2 x64, Emscripten 6 / WebAssembly under Node). Every number
is from 1,000 seeded matches of 600 ticks with `replay 1000 600`.

```bash
cmake -S . -B build-risky -DLOCKSTEP_RISKY=unordered   # or float
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
| gcc (libstdc++) | 881 / 1000 |
| Emscripten (libc++) | 860 / 1000 |

gcc and Emscripten also differ from each other in 249 matches.

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
| gcc with `-mfma -ffp-contract=fast` vs plain gcc | **12 / 1000** |
| *fixed-point build* with the same flags vs normal | 0 / 1000 |

On these three targets, float was deterministic. That is expected and not a
contradiction of note 01: all three use SSE2 or WebAssembly scalar floats
with IEEE rounding, `sqrt` is correctly rounded, and none of them contracts
`a*b + c` by default (MSVC's `/fp:precise`, and ISO `-std=c++20` mode in gcc
and clang, keep `-ffp-contract` off).

One flag changes that. With `-mfma -ffp-contract=fast` gcc is allowed to fuse
multiply and add into one instruction with a single rounding; the object file
then contains 3 FMA instructions (0 without the flag), and 12 of 1,000
matches end differently. The fixed-point build, compiled with the same flags,
does not move: there is no float arithmetic for the flag to change. So the
rule in note 01 is not about these three compilers today; it is about the
build flag, the compiler upgrade or the ARM target that has not been added
yet.

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
