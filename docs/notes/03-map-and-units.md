# 03 — Map and units

**What:** `World` now holds a 16×16 tile map, six units (one Soldier, Archer
and Knight per player) and the simulation's `Pcg32`. `make_world(seed)` builds
it; `hash()` covers every field. Commands and combat come in step 04, so
`step()` still mixes inputs into a placeholder counter.

**Map generation draws in a fixed order.** Every interior tile consumes one
random number whether or not it becomes a wall. If generation instead drew
"until it found a free tile", the number of draws would depend on earlier
outcomes, so any change to one rule would shift every later random number.
With one draw per tile, the RNG position after generation is the same for
every seed.

**Units live in a `std::vector` sorted by id.** Iteration order is part of
the simulation: units act in that order, and the hash reads them in that
order. `std::unordered_map` iterates by bucket, and MSVC's STL and libc++
use different bucket layouts and growth policies, so the same contents come
back in different orders. A sorted vector has one order everywhere.

**Positions are `Fixed`, in tile units.** A unit stands at a tile centre
(`tile + 0.5`); the integer part is the tile it occupies
(`x.raw >> 16`), which is how collision with walls will be read.

**The hash test proves coverage.** "Same seed, same hash" passes even if a
field is left out of the hash: both sides leave it out. So the test changes
each field of the state on its own (tick, a tile, unit count, every unit field,
the RNG, the placeholder) and requires the hash to change every time. Leaving
`hp` out of `hash()` makes exactly that case fail (checked once by hand). A
field the hash misses is a desync the CI cannot see.

**Measured:** gcc 16 and Emscripten 6 agree on all 1,000 seeds after 600
ticks, and the 1,000 final hashes are all distinct.

**Questions this answers in an interview**
- Why can `unordered_map` break a lockstep game when its contents are identical?
- How do you test that a hash function covers the whole state?
- Why should procedural generation consume randomness in a fixed pattern?
