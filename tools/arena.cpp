// Plays N seeded matches, Blue random against Orange the AI, and counts who
// wins and how many were won on the objective. The number this prints is the
// one the README quotes.
//
//   arena [seeds] [ticks]     defaults: 1000 seeds, 600 ticks
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "lockstep/ai.hpp"
#include "lockstep/world.hpp"

int main(int argc, char** argv) {
    const std::uint32_t seeds = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 1000;
    const std::uint32_t ticks = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 600;
    int blue = 0, orange = 0, draw = 0, on_objective = 0;
    std::uint64_t total_ticks = 0;
    for (std::uint32_t seed = 0; seed < seeds; ++seed) {
        std::uint64_t rng = seed;
        lockstep::World w = lockstep::make_world(seed);
        std::uint32_t t = 0;
        for (; t < ticks && lockstep::winner(w) == -1; ++t) {
            lockstep::step(w, lockstep::match_orders(w, rng));
        }
        total_ticks += t;
        int win = lockstep::winner(w);
        if (win == 0 || win == 1) {
            if (w.held[static_cast<std::size_t>(win)] >= lockstep::kHoldToWin) ++on_objective;
        } else if (win == -1) {
            // Out of ticks: more units left wins.
            int a = 0, b = 0;
            for (const auto& u : w.units) (u.owner == 0 ? a : b)++;
            win = a > b ? 0 : b > a ? 1 : 2;
        }
        (win == 0 ? blue : win == 1 ? orange : draw)++;
    }
    std::printf("%u matches: AI (orange) %d, random (blue) %d, draw %d; %d won on the objective; mean length %.0f ticks\n",
                seeds, orange, blue, draw, on_objective, static_cast<double>(total_ticks) / seeds);
    return 0;
}
