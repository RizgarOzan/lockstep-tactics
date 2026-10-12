#pragma once
// Test input, not simulation: random orders for both players, used by the
// replay tools and the tests. Each player orders one random unit on about a
// quarter of the ticks; half the orders move to a random tile, half attack a
// random enemy.
#include <cstdint>
#include <vector>

#include "lockstep/world.hpp"

namespace lockstep {

inline std::uint64_t splitmix64(std::uint64_t& s) {
    std::uint64_t z = (s += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

inline std::vector<Command> random_orders(const World& world, std::uint64_t& rng) {
    std::vector<Command> commands;
    std::vector<std::uint32_t> mine, theirs;
    for (std::uint32_t p = 0; p < 2; ++p) {
        if (splitmix64(rng) % 4 != 0) continue;
        mine.clear();
        theirs.clear();
        for (const Unit& u : world.units) {
            (u.owner == p ? mine : theirs).push_back(u.id);
        }
        if (mine.empty()) continue;
        const std::uint32_t unit = mine[splitmix64(rng) % mine.size()];
        if (theirs.empty() || splitmix64(rng) % 2 == 0) {
            const auto tx = static_cast<std::int32_t>(1 + splitmix64(rng) % 14);
            const auto ty = static_cast<std::int32_t>(1 + splitmix64(rng) % 14);
            commands.push_back(Command::move(p, unit, tx, ty));
        } else {
            commands.push_back(Command::attack(p, unit, theirs[splitmix64(rng) % theirs.size()]));
        }
    }
    return commands;
}

}  // namespace lockstep
