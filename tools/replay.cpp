// Runs N seeded matches and prints one line per seed:
// "<seed> <final hash> <units left>".
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

    std::vector<lockstep::Command> commands;
    std::vector<std::uint32_t> mine, theirs;
    for (std::uint32_t seed = 0; seed < seeds; ++seed) {
        std::uint64_t rng = seed;
        lockstep::World world = lockstep::make_world(seed);
        for (std::uint32_t t = 0; t < ticks; ++t) {
            // Each player gives one order to a random unit on a quarter of the
            // ticks: half moves to a random tile, half attacks a random enemy.
            commands.clear();
            for (std::uint32_t p = 0; p < 2; ++p) {
                if (splitmix64(rng) % 4 != 0) continue;
                mine.clear();
                theirs.clear();
                for (const lockstep::Unit& u : world.units) {
                    (u.owner == p ? mine : theirs).push_back(u.id);
                }
                if (mine.empty()) continue;
                const std::uint32_t unit = mine[splitmix64(rng) % mine.size()];
                if (theirs.empty() || splitmix64(rng) % 2 == 0) {
                    const auto tx = static_cast<std::int32_t>(1 + splitmix64(rng) % 14);
                    const auto ty = static_cast<std::int32_t>(1 + splitmix64(rng) % 14);
                    commands.push_back(lockstep::Command::move(p, unit, tx, ty));
                } else {
                    commands.push_back(lockstep::Command::attack(p, unit, theirs[splitmix64(rng) % theirs.size()]));
                }
            }
            lockstep::step(world, commands);
        }
        std::printf("%" PRIu32 " %016" PRIx64 " %zu\n", seed, lockstep::hash(world), world.units.size());
    }
    return 0;
}
