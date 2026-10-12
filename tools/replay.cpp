// Runs N seeded matches and prints one line per seed:
// "<seed> <final hash> <units left>".
// CI runs this under MSVC x64 and under WebAssembly (Node) and diffs the two
// outputs; any differing line is a desync.
//
//   replay [seeds] [ticks]     defaults: 1000 seeds, 600 ticks
#include <cinttypes>
#include <cstdio>
#include <cstdlib>

#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"

int main(int argc, char** argv) {
    const std::uint32_t seeds = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 1000;
    const std::uint32_t ticks = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 600;

    for (std::uint32_t seed = 0; seed < seeds; ++seed) {
        std::uint64_t rng = seed;
        lockstep::World world = lockstep::make_world(seed);
        for (std::uint32_t t = 0; t < ticks; ++t) {
            lockstep::step(world, lockstep::random_orders(world, rng));
        }
        std::printf("%" PRIu32 " %016" PRIx64 " %zu\n", seed, lockstep::hash(world), world.units.size());
    }
    return 0;
}
