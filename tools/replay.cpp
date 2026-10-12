// Runs N seeded matches and prints one line per seed: "<seed> <final hash>".
// CI runs this under MSVC x64 and under WebAssembly (Node) and diffs the two
// outputs; any differing line is a desync.
//
//   replay [seeds] [ticks]     defaults: 1000 seeds, 600 ticks
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "lockstep/world.hpp"

namespace {

// splitmix64: generates the test inputs, not part of the simulation.
std::uint64_t splitmix64(std::uint64_t& s) {
    std::uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint32_t seeds = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 1000;
    const std::uint32_t ticks = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 600;

    std::vector<lockstep::Input> inputs(2);
    for (std::uint32_t seed = 0; seed < seeds; ++seed) {
        std::uint64_t rng = seed;
        lockstep::World world;
        for (std::uint32_t t = 0; t < ticks; ++t) {
            for (std::uint32_t p = 0; p < inputs.size(); ++p) {
                inputs[p] = {p, static_cast<std::int32_t>(splitmix64(rng) % 16)};
            }
            lockstep::step(world, inputs);
        }
        std::printf("%" PRIu32 " %016" PRIx64 "\n", seed, lockstep::hash(world));
    }
    return 0;
}
