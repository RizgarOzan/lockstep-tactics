# 01 — Fixed-point numbers

**What:** `lockstep::Fixed` (`core/include/lockstep/fixed.hpp`), Q16.16 in an
`int32_t`: 16 integer bits, 16 fraction bits, a step of 1/65536.

**Why not float.** IEEE 754 itself is deterministic, but the code a compiler
emits around it is not pinned down: it may contract `a*b+c` into one fused
multiply-add (one rounding instead of two), keep intermediates in wider
registers, or reorder operations under fast-math. MSVC x64 and clang targeting
WebAssembly make different choices, and one differing bit in a position
compounds every tick. Integer arithmetic has exactly one answer.

**Why Q16.16.** A tactics map measured in tiles needs a few thousand units of
range and sub-tile precision; ±32768 with 1/65536 steps covers both and keeps
every product inside an `int64_t` before the shift.

**The rounding decision.** `a * b` is computed as `(int64)a * b >> 16`. Since
C++20, `>>` on a negative value is defined as arithmetic, so it floors. C++
integer division truncates toward zero instead, which would make `-1/3` the
mirror of `1/3` while `-1 * 0.5` floors: two different rules in one type. The
division therefore corrects truncation to floor, and both operators round
toward negative infinity. The tests pin `-1/3 == -21846` steps; removing the
correction makes that test fail (checked by hand once).

**Overflow** is not trapped. `+` and `-` go through `uint32_t`, where wrap is
defined, and the `int64 -> int32` narrowing is modular since C++20, so an
overflow produces the same wrong number on every target: a simulation bug, not
a desync.

**Questions this answers in an interview**
- Why does a lockstep game avoid floats? (Above, and: it is not the format, it
  is the freedom the compiler has around it.)
- What does `-7 >> 1` give in C++? (`-4` since C++20; implementation-defined before.)
- What does `-7 / 2` give? (`-3`, truncation; hence the explicit floor.)
