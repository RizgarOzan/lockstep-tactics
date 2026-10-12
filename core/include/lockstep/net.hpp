#pragma once
// Lockstep over any ordered, reliable channel of text frames. Each side
// sends one frame per tick holding its orders for a tick `delay` ahead; a
// tick runs only when both sides' orders for it are in. Nothing here talks
// to a socket: the viewer hands frames to a WebRTC data channel in the
// browser, and the tests pass them between two Lockstep objects in memory.
// The frames carry the state hash of the last tick each side ran, so a
// desync is noticed within `delay` ticks, not at the end of the match.
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "lockstep/world.hpp"

namespace lockstep {

class Lockstep {
public:
    // `local` is this side's player number (0 or 1). `delay` is how many
    // ticks ahead orders are scheduled; it must be the same on both sides and
    // at least 1, and ticks below it run with no orders.
    Lockstep(std::uint32_t local, std::uint32_t delay);

    std::uint32_t local() const { return local_; }
    std::uint32_t delay() const { return delay_; }
    // The tick next() would run next.
    std::uint32_t next_tick() const { return next_; }

    // Schedules `orders` for the tick next_tick() + delay and returns the
    // frame to send. Returns an empty string when that tick already has local
    // orders (the caller is polling faster than the peer answers); the caller
    // keeps its orders for the next call.
    std::string submit(const std::vector<Command>& orders);

    // Records the hash after the tick that just ran; it rides in the next
    // frame and is compared with the peer's.
    void ran(std::uint32_t tick, std::uint64_t hash);

    // One frame from the peer. false means it was malformed or not ours.
    bool receive(std::string_view frame);

    // Both sides' orders for next_tick(), sorted the way step() expects to
    // receive them, or nothing while the peer's frame is still missing.
    // Advances next_tick() on success.
    std::optional<std::vector<Command>> next();

    // True once the peer's orders for next_tick() are in.
    bool ready() const;

    // The first tick whose hash the two sides disagreed on, if any.
    std::optional<std::uint32_t> desync() const { return desync_; }

private:
    std::uint32_t local_;
    std::uint32_t delay_;
    std::uint32_t next_ = 0;
    std::map<std::uint32_t, std::vector<Command>> local_orders_;
    std::map<std::uint32_t, std::vector<Command>> remote_orders_;
    std::map<std::uint32_t, std::uint64_t> local_hashes_;
    std::optional<std::uint32_t> desync_;
};

}  // namespace lockstep
