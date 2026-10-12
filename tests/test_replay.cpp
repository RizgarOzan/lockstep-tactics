#include <doctest/doctest.h>

#include <cstdint>
#include <string>

#include "lockstep/fields.hpp"
#include "lockstep/random_orders.hpp"
#include "lockstep/replay.hpp"

using namespace lockstep;

namespace {

Replay sample(std::uint64_t seed, std::uint32_t ticks) {
    std::uint64_t orders_rng = seed;
    return record(seed, ticks, [&](const World& w) { return random_orders(w, orders_rng); });
}

}  // namespace

TEST_CASE("a recorded replay plays back to the same hash on every tick") {
    const Replay r = sample(3, 300);
    REQUIRE(r.hashes.size() == 300);
    CHECK(play(r) == r.hashes);
    CHECK_FALSE(first_divergence(r.hashes, play(r)).has_value());
}

TEST_CASE("the text format round-trips") {
    const Replay r = sample(4, 200);
    const auto back = from_text(to_text(r));
    REQUIRE(back.has_value());
    CHECK(back->seed == r.seed);
    CHECK(back->hashes == r.hashes);
    CHECK(back->commands.size() == r.commands.size());
    CHECK(to_text(*back) == to_text(r));
}

TEST_CASE("garbage is rejected, not half-read") {
    CHECK_FALSE(from_text("").has_value());
    CHECK_FALSE(from_text("lockstep-replay 1\nseed x\n").has_value());
    std::string text = to_text(sample(5, 10));
    text.resize(text.size() / 2);
    CHECK_FALSE(from_text(text).has_value());
}

TEST_CASE("two identical worlds have no difference; one field changed is named") {
    World a = make_world(6);
    World b = a;
    CHECK(first_difference(a, b).empty());
    b.units[2].hp -= 1;
    CHECK(first_difference(a, b).rfind("unit#3.hp", 0) == 0);
    World c = a;
    c.map.set(7, 4, c.map.at(7, 4) == Tile::Wall ? Tile::Floor : Tile::Wall);
    CHECK(first_difference(a, c).rfind("map[7,4]", 0) == 0);
}

TEST_CASE("the bisector finds the tick and the field of 100 injected desyncs") {
    // Each case corrupts one field right after one tick, the way a desync
    // would leave a tick's result wrong on one machine, and asks locate() for
    // the tick and the field. Every one must come back exact.
    const Replay r = sample(8, 400);
    std::uint64_t pick = 12345;
    int exact = 0;
    const int cases = 100;
    for (int i = 0; i < cases; ++i) {
        const std::uint32_t tick = 1 + static_cast<std::uint32_t>(splitmix64(pick) % 400);
        const int kind = static_cast<int>(splitmix64(pick) % 7);
        const std::uint64_t which = splitmix64(pick);
        std::string expected;
        const auto corrupt = [&](World& w) {
            if (w.tick != tick) return;
            if (kind == 0 || w.units.empty()) {
                const int x = 1 + static_cast<int>(which % 14), y = 1 + static_cast<int>((which >> 8) % 14);
                w.map.set(x, y, w.map.at(x, y) == Tile::Wall ? Tile::Floor : Tile::Wall);
                expected = "map[" + std::to_string(x) + "," + std::to_string(y) + "]";
                return;
            }
            Unit& u = w.units[which % w.units.size()];
            const std::string unit = "unit#" + std::to_string(u.id) + ".";
            switch (kind) {
                case 1: u.hp += 1; expected = unit + "hp"; break;
                case 2: u.x = u.x + Fixed::from_raw(1); expected = unit + "x"; break;
                case 3: u.y = u.y - Fixed::from_raw(1); expected = unit + "y"; break;
                case 4: u.cooldown += 1; expected = unit + "cooldown"; break;
                case 5: u.target_x += 1; expected = unit + "target_x"; break;
                default: w.rng.next(); expected = "rng.state"; break;
            }
        };
        const auto found = locate(r, corrupt);
        CAPTURE(i);
        CAPTURE(tick);
        CAPTURE(expected);
        REQUIRE(found.has_value());
        CHECK(found->tick == tick);
        CHECK(found->field.rfind(expected, 0) == 0);
        exact += found->tick == tick && found->field.rfind(expected, 0) == 0;
    }
    CHECK(exact == cases);
}
