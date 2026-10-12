// Measures the simulation step alone. The orders for every tick are generated
// first and kept in memory, so the loop being timed is only step(); the
// random input generator and the hash are left out on purpose, because the
// replay tool already pays for those and neither is the simulation.
//
//   bench [seeds] [ticks] [repeats]     defaults: 200 seeds, 600 ticks, 20 repeats
//
// Prints one line per repeat (ns per tick) and then min, median and max, plus
// the hash of every final state folded into one number: an optimisation that
// changes that number changed the simulation, not just its speed.
#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"

namespace {

const char* compiler() {
#if defined(__EMSCRIPTEN__)
    return "emscripten";
#elif defined(__clang__)
    return "clang " __clang_version__;
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#elif defined(_MSC_VER)
    return "msvc";
#else
    return "unknown";
#endif
}

}  // namespace

int main(int argc, char** argv) {
    const std::uint32_t seeds = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 200;
    const std::uint32_t ticks = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 600;
    const int repeats = argc > 3 ? std::atoi(argv[3]) : 20;

    // Orders depend on the state they were generated against, so they are
    // recorded by playing each match once; the timed loop replays them.
    std::vector<std::vector<std::vector<lockstep::Command>>> orders(seeds);
    for (std::uint32_t seed = 0; seed < seeds; ++seed) {
        std::uint64_t rng = seed;
        lockstep::World world = lockstep::make_world(seed);
        orders[seed].reserve(ticks);
        for (std::uint32_t t = 0; t < ticks; ++t) {
            orders[seed].push_back(lockstep::random_orders(world, rng));
            lockstep::step(world, orders[seed].back());
        }
    }

    std::vector<double> ns_per_tick;
    std::uint64_t folded = 0;
    for (int r = 0; r < repeats; ++r) {
        std::uint64_t fold = 0;
        const auto start = std::chrono::steady_clock::now();
        for (std::uint32_t seed = 0; seed < seeds; ++seed) {
            lockstep::World world = lockstep::make_world(seed);
            for (std::uint32_t t = 0; t < ticks; ++t) lockstep::step(world, orders[seed][t]);
            fold = fold * 1099511628211ULL ^ lockstep::hash(world);
        }
        const auto end = std::chrono::steady_clock::now();
        const double ns = std::chrono::duration<double, std::nano>(end - start).count();
        ns_per_tick.push_back(ns / (static_cast<double>(seeds) * ticks));
        if (r == 0) folded = fold;
        if (fold != folded) {
            std::printf("repeat %d produced a different state hash\n", r);
            return 1;
        }
        std::printf("repeat %2d  %8.1f ns/tick\n", r, ns_per_tick.back());
    }
    std::sort(ns_per_tick.begin(), ns_per_tick.end());
    const double median = ns_per_tick[ns_per_tick.size() / 2];
    std::printf("\n%s | %" PRIu32 " seeds x %" PRIu32 " ticks x %d repeats\n", compiler(), seeds, ticks, repeats);
    std::printf("min %.1f  median %.1f  max %.1f ns/tick   final-state fold %016" PRIx64 "\n",
                ns_per_tick.front(), median, ns_per_tick.back(), folded);
    return 0;
}
