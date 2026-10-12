#pragma once
// A replay is a seed, every tick's commands and the state hash after every
// tick. Playing it back anywhere must reproduce each hash; the first tick that
// does not is where a desync started, and comparing the two states at that
// tick names the field.
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lockstep/world.hpp"

namespace lockstep {

struct Replay {
    std::uint64_t seed = 0;
    std::vector<std::vector<Command>> commands;  // one entry per tick
    std::vector<std::uint64_t> hashes;           // hash after each tick
};

using OrderSource = std::function<std::vector<Command>(const World&)>;
// Called with the world right after each tick; tests use it to inject a desync.
using AfterStep = std::function<void(World&)>;

Replay record(std::uint64_t seed, std::uint32_t ticks, const OrderSource& orders);

// Hash after each tick when the replay's commands are played from its seed.
std::vector<std::uint64_t> play(const Replay& r, const AfterStep& after = {});

// The world after `ticks` ticks of the replay.
World state_at(const Replay& r, std::uint32_t ticks, const AfterStep& after = {});

// The tick (1-based: the world's tick counter) of the first differing hash.
std::optional<std::uint32_t> first_divergence(const std::vector<std::uint64_t>& a,
                                              const std::vector<std::uint64_t>& b);

struct Desync {
    std::uint32_t tick;
    std::string field;  // as first_difference() reports it
};

// Plays `r` with `after` applied and reports where it first leaves the
// recorded hashes, and which field differs at that tick.
std::optional<Desync> locate(const Replay& r, const AfterStep& after);

std::string to_text(const Replay& r);
std::optional<Replay> from_text(std::string_view text);

}  // namespace lockstep
