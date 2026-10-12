#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "lockstep/player.hpp"
#include "lockstep/random_orders.hpp"
#include "lockstep/replay.hpp"

using namespace lockstep;

TEST_CASE("unit_at finds the unit on a tile and nothing on an empty one") {
    const World w = make_world(1);
    // Blue spawns in column 1, rows 5, 7, 9; orange in column 14.
    REQUIRE(unit_at(w, 1, 5) != nullptr);
    CHECK(unit_at(w, 1, 5)->id == 1);
    CHECK(unit_at(w, 14, 7)->id == 5);
    CHECK(unit_at(w, 1, 6) == nullptr);
    CHECK(unit_at(w, 0, 0) == nullptr);
}

TEST_CASE("clicking an own unit selects it, an enemy or a tile then orders it") {
    const World w = make_world(1);
    Click c = click(w, 0, 0, 1, 7);
    CHECK(c.selected == 2);
    CHECK_FALSE(c.command.has_value());

    c = click(w, 0, 2, 14, 9);
    CHECK(c.selected == 2);
    REQUIRE(c.command.has_value());
    CHECK(c.command->order == Order::Attack);
    CHECK(c.command->unit_id == 2);
    CHECK(c.command->target_id == 6);
    CHECK(c.command->player == 0);

    c = click(w, 0, 2, 1, 12);
    REQUIRE(c.command.has_value());
    CHECK(c.command->order == Order::Move);
    CHECK(c.command->target_x == 1);
    CHECK(c.command->target_y == 12);

    // Orange's own unit, seen from orange: a selection, not an attack.
    c = click(w, 1, 0, 14, 5);
    CHECK(c.selected == 4);
    CHECK_FALSE(c.command.has_value());
}

TEST_CASE("walls, the outside and an empty selection give no order") {
    const World w = make_world(1);
    CHECK_FALSE(click(w, 0, 0, 1, 12).command.has_value());  // nothing selected
    CHECK_FALSE(click(w, 0, 1, 0, 0).command.has_value());   // edge wall
    CHECK_FALSE(click(w, 0, 1, -1, 3).command.has_value());  // off the board
    CHECK(click(w, 0, 1, 99, 3).selected == 1);              // selection kept
    CHECK_FALSE(click(w, 0, 0, 14, 5).command.has_value());  // enemy, nothing selected
}

TEST_CASE("a match played by clicks records a replay that verifies like any other") {
    // Blue is driven by clicks, orange by the random player; both end up as
    // Commands in the replay, so playback must reproduce every hash.
    std::uint64_t rng = 9;
    std::uint32_t selected = 0;
    std::uint32_t t = 0;
    const Replay r = record(9, 300, [&](const World& w) {
        std::vector<Command> out = random_orders(w, rng);
        std::erase_if(out, [](const Command& c) { return c.player == 0; });
        // Every 20 ticks: pick blue's lowest unit, then send it at orange's.
        if (t % 20 == 0) {
            for (const Unit& u : w.units) {
                if (u.owner == 0) {
                    selected = click(w, 0, 0, u.x.raw >> Fixed::kFracBits, u.y.raw >> Fixed::kFracBits).selected;
                    break;
                }
            }
        } else if (t % 20 == 1 && selected != 0) {
            for (const Unit& u : w.units) {
                if (u.owner == 1) {
                    const Click c = click(w, 0, selected, u.x.raw >> Fixed::kFracBits, u.y.raw >> Fixed::kFracBits);
                    if (c.command) out.push_back(*c.command);
                    break;
                }
            }
        }
        ++t;
        return out;
    });
    std::size_t blue_orders = 0;
    for (const auto& tick : r.commands) {
        for (const Command& c : tick) blue_orders += c.player == 0;
    }
    CHECK(blue_orders > 0);
    CHECK(play(r) == r.hashes);
    const auto back = from_text(to_text(r));
    REQUIRE(back.has_value());
    CHECK(play(*back) == r.hashes);
}
