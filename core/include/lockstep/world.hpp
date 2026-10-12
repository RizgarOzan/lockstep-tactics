#pragma once
// The simulation's public surface: a world, the inputs one tick consumes, a
// step function and a hash of the full state. Everything the replay tool and
// the CI comparison need goes through these four names.
//
// The state below is a placeholder that only proves the pipeline end to end.
// The real simulation (map, units, fixed-point movement and combat) replaces
// it; the four names stay.
#include <cstdint>
#include <span>

namespace lockstep {

struct Input {
    std::uint32_t player = 0;
    std::int32_t command = 0;
};

struct World {
    std::uint32_t tick = 0;
    std::int32_t placeholder = 0;
};

void step(World& world, std::span<const Input> inputs);
std::uint64_t hash(const World& world);

}  // namespace lockstep
