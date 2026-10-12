#pragma once
// The built-in opponent. It reads the World with integer maths only and uses
// no random numbers, so the same world gives the same orders on every
// machine; what it decides comes out as ordinary Commands that go through
// step() exactly like a player's clicks. It is an order source, not part of
// the simulation: a replay stores its commands, never its reasoning.
#include <cstdint>
#include <vector>

#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"

namespace lockstep {

// Orders for `player`'s units this tick: only the ones that change what a
// unit is already doing, so a long replay does not fill with repeats.
std::vector<Command> ai_orders(const World& w, std::uint32_t player);

// What the replay tools and the CI play: Blue takes random orders, Orange
// plays the AI. Both end up in the same command list.
inline std::vector<Command> match_orders(const World& w, std::uint64_t& rng) {
    std::vector<Command> out = random_orders(w, rng);
    std::erase_if(out, [](const Command& c) { return c.player == 1; });
    const std::vector<Command> ai = ai_orders(w, 1);
    out.insert(out.end(), ai.begin(), ai.end());
    return out;
}

}  // namespace lockstep
