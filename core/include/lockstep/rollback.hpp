#pragma once
// Rollback on the same frames as Lockstep: a local order runs on the tick
// it was given, the peer's orders for that tick arrive later, and the ticks
// since are re-simulated from the last confirmed state. No prediction of
// the peer beyond "no new orders": units keep doing what they were doing,
// which is what a tick without orders means in step() anyway. A tick is
// confirmed once both sides' orders for it are known, and only confirmed
// ticks have a hash both sides can compare; the viewer shows the predicted
// present and the replay holds the confirmed past.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lockstep/replay.hpp"
#include "lockstep/world.hpp"

namespace lockstep {

class Rollback {
public:
    // `max_lag` is how far the present may run ahead of the last confirmed
    // tick before advance() refuses to go on; it bounds the re-simulation.
    Rollback(std::uint32_t local, std::uint64_t seed, std::uint32_t max_lag = 30);

    std::uint32_t local() const { return local_; }
    // The predicted present: confirmed ticks plus local orders since.
    const World& world() const { return present_; }
    std::uint32_t tick() const { return present_.tick; }
    // Ticks whose orders both sides have; the hashes up to here are final.
    std::uint32_t confirmed() const { return confirmed_; }
    bool may_advance() const { return present_.tick - confirmed_ < max_lag_; }

    // Runs one tick now with the local orders, and returns the frame to send
    // (the same format as Lockstep's, for this tick). Empty when the lag is
    // at its limit; the caller keeps its orders and tries again later.
    std::string advance(const std::vector<Command>& orders);

    // A frame from the peer. If it is for a tick already run, the ticks from
    // there to the present are re-simulated with the orders now known.
    // false means malformed, ours, or for a tick older than what is kept.
    bool receive(std::string_view frame);

    // The first confirmed tick whose hash the two sides disagreed on.
    std::optional<std::uint32_t> desync() const { return desync_; }
    // Ticks re-simulated so far: the cost of guessing, for the screen.
    std::uint64_t resimulated() const { return resimulated_; }

    // The confirmed part of the match as a replay: seed, both sides' orders
    // per tick and the hash after each, exactly what a Lockstep match saves.
    Replay replay() const { return replay_; }

private:
    void confirm_and_resimulate();

    std::uint32_t local_;
    std::uint32_t max_lag_;
    World base_;     // the state after tick confirmed_
    World present_;  // base_ plus every tick since, with the orders known so far
    std::uint32_t confirmed_ = 0;
    std::map<std::uint32_t, std::vector<Command>> local_orders_;   // ticks > confirmed_
    std::map<std::uint32_t, std::vector<Command>> remote_orders_;  // ticks > confirmed_, as they arrive
    std::map<std::uint32_t, std::uint64_t> remote_hashes_;         // the peer's claim per confirmed tick
    Replay replay_;
    std::optional<std::uint32_t> desync_;
    std::uint64_t resimulated_ = 0;
};

}  // namespace lockstep
