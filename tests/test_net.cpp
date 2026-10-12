#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "lockstep/net.hpp"
#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"

using namespace lockstep;

namespace {

// One side of a two-browser match: its own world, its own Lockstep, and a
// random player that only ever orders its own units.
struct Side {
    Lockstep sync;
    World world;
    std::uint64_t rng;
    std::vector<std::uint64_t> hashes;
    Side(std::uint32_t player, std::uint32_t delay, std::uint64_t seed)
        : sync(player, delay), world(make_world(seed)), rng(seed * 7 + player) {}

    std::string frame() {
        std::vector<Command> mine = random_orders(world, rng);
        std::erase_if(mine, [&](const Command& c) { return c.player != sync.local(); });
        return sync.submit(mine);
    }
    bool tick() {
        const auto cmds = sync.next();
        if (!cmds) return false;
        step(world, *cmds);
        hashes.push_back(hash(world));
        sync.ran(world.tick, hashes.back());
        return true;
    }
};

}  // namespace

TEST_CASE("two sides exchanging frames run the same match tick for tick") {
    Side a(0, 3, 11), b(1, 3, 11);
    for (int t = 0; t < 200; ++t) {
        const std::string fa = a.frame(), fb = b.frame();
        REQUIRE_FALSE(fa.empty());
        REQUIRE_FALSE(fb.empty());
        REQUIRE(b.sync.receive(fa));
        REQUIRE(a.sync.receive(fb));
        REQUIRE(a.tick());
        REQUIRE(b.tick());
    }
    CHECK(a.hashes == b.hashes);
    CHECK(a.world.tick == 200);
    CHECK_FALSE(a.sync.desync().has_value());
    CHECK_FALSE(b.sync.desync().has_value());
}

TEST_CASE("a tick waits for the peer's frame and does not run twice") {
    // The protocol: every tick, submit() once, then next(). The first `delay`
    // ticks run without frames, but their submits already schedule ahead.
    Side a(0, 2, 5), b(1, 2, 5);
    for (int t = 0; t < 2; ++t) {
        CHECK_FALSE(a.frame().empty());
        CHECK(a.tick());
    }
    CHECK(a.sync.next_tick() == 2);
    CHECK_FALSE(a.frame().empty());  // orders for tick 4
    CHECK(a.frame().empty());        // tick 4 already submitted
    CHECK_FALSE(a.tick());           // tick 2 waits for b's frame
    CHECK_FALSE(a.tick());
    REQUIRE(a.sync.receive(b.frame()));  // b's orders for tick 2
    CHECK(a.tick());
    CHECK(a.sync.next_tick() == 3);
    CHECK_FALSE(a.tick());  // and tick 3 waits again
}

TEST_CASE("orders are scheduled delay ticks ahead and only for own units") {
    Lockstep s(1, 3);
    const std::string f = s.submit({Command::move(1, 4, 5, 5), Command::move(0, 1, 2, 2)});
    CHECK(f == "f 1 3 1 0 0000000000000000\nc 1 4 1 5 5 0\n");
    Lockstep peer(0, 3);
    REQUIRE(peer.receive(f));
    for (int i = 0; i < 3; ++i) {
        CHECK_FALSE(peer.submit({}).empty());
        REQUIRE(peer.next().has_value());
    }
    const auto cmds = peer.next();  // tick 3: both sides' orders are in
    REQUIRE(cmds.has_value());
    REQUIRE(cmds->size() == 1);
    CHECK(cmds->at(0).unit_id == 4);
    CHECK(cmds->at(0).player == 1);
}

TEST_CASE("a hash that disagrees names the tick, within delay ticks") {
    Side a(0, 2, 9), b(1, 2, 9);
    for (int t = 0; t < 30; ++t) {
        const std::string fa = a.frame(), fb = b.frame();
        REQUIRE(b.sync.receive(fa));
        REQUIRE(a.sync.receive(fb));
        a.tick();
        b.tick();
        if (b.world.tick == 20) b.world.units[0].hp -= 1;  // b drifts after tick 20
    }
    REQUIRE(a.sync.desync().has_value());
    CHECK(*a.sync.desync() == 21);
    REQUIRE(b.sync.desync().has_value());
    CHECK(*b.sync.desync() == 21);
}

TEST_CASE("garbage and frames from ourselves are refused") {
    Lockstep s(0, 1);
    CHECK_FALSE(s.receive(""));
    CHECK_FALSE(s.receive("f 0 1 0 0 0\n"));         // our own player number
    CHECK_FALSE(s.receive("f 1 1 1 0 0\n"));         // promises a command, has none
    CHECK_FALSE(s.receive("f 1 1 1 0 0\nc 0 1 1 2 2 0\n"));  // command for the wrong player
    CHECK(s.receive("f 1 1 0 0 0\n"));
}
