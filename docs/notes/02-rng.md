# 02 — Random numbers

**What:** `lockstep::Pcg32` (`core/include/lockstep/rng.hpp`), the PCG32
generator written out in about thirty lines, with an unbiased `below(n)`.

**Why not `<random>`.** The standard fixes the *engines*: `std::mt19937`
produces the same sequence everywhere. It does not fix the *distributions*.
`std::uniform_int_distribution<int>(0, 5)` may consume a different number of
engine outputs, or map them differently, in MSVC's STL than in libc++ (which
Emscripten uses). Same seed, different dice roll, desync on the first hit
chance. Owning both the engine and the mapping removes the question.

**Why PCG32.** Small state (two `uint64_t`, trivially hashed and saved in a
replay), fast, good statistical quality, and a published reference output to
test against: the tests check the first six numbers of the reference
`pcg32-demo` (seed 42, stream 54). Changing one shift in the output function
fails six of those checks (checked once by hand).

**Unbiased bounds.** `next() % 6` favours small values, because 2^32 is not a
multiple of 6. `below(n)` redraws the few values under `2^32 mod n`, which
is `(0u - n) % n` in unsigned arithmetic, the same trick the reference
`pcg32_boundedrand_r` uses. On average it draws barely more than once.

**Unsigned on purpose.** The rotate is `(x >> r) | (x << ((0u - r) & 31))`.
Writing `x << (32 - r)` would shift by 32 when `r == 0`, which is undefined
behaviour; the mask keeps every shift below 32.

**Questions this answers in an interview**
- Is `std::mt19937` deterministic across platforms? (The engine yes, the
  distributions no.)
- What is modulo bias and how do you remove it?
- Why is `x << 32` on a 32-bit value a bug even if it "works" on your machine?
