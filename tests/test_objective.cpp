#include <doctest/doctest.h>

#include <vector>

#include "lockstep/world.hpp"

using namespace lockstep;

namespace {

World empty_room() {
    World w;
    for (int y = 0; y < Map::kHeight; ++y) {
        for (int x = 0; x < Map::kWidth; ++x) {
            const bool edge = x == 0 || y == 0 || x == Map::kWidth - 1 || y == Map::kHeight - 1;
            w.map.set(x, y, edge ? Tile::Wall : Tile::Floor);
        }
    }
    return w;
}

Fixed centre(int tile) { return Fixed::from_int(tile) + Fixed::from_ratio(1, 2); }

Unit& add_unit(World& w, std::uint8_t owner, int tx, int ty) {
    const auto id = static_cast<std::uint32_t>(w.units.size() + 1);
    w.units.push_back(Unit{id, owner, UnitType::Knight, stats(UnitType::Knight).max_hp, centre(tx), centre(ty)});
    return w.units.back();
}

}  // namespace

TEST_CASE("the objective is the 2x2 tiles in the middle of the map") {
    int tiles = 0;
    for (int y = 0; y < Map::kHeight; ++y) {
        for (int x = 0; x < Map::kWidth; ++x) tiles += on_objective(x, y) ? 1 : 0;
    }
    CHECK(tiles == 4);
    CHECK(on_objective(Map::kWidth / 2 - 1, Map::kHeight / 2 - 1));
    CHECK(on_objective(Map::kWidth / 2, Map::kHeight / 2));
}

TEST_CASE("on every map the objective is floor and both spawns can walk to it") {
    for (std::uint64_t seed = 0; seed < 1000; ++seed) {
        const World w = make_world(seed);
        CAPTURE(seed);
        for (int y = 0; y < Map::kHeight; ++y) {
            for (int x = 0; x < Map::kWidth; ++x) {
                if (!on_objective(x, y)) continue;
                REQUIRE(w.map.at(x, y) == Tile::Floor);
                REQUIRE(next_step(w.map, 1, 5, x, y).has_value());
                REQUIRE(next_step(w.map, Map::kWidth - 2, 5, x, y).has_value());
            }
        }
    }
}

TEST_CASE("a side alone on the objective holds it one tick per tick") {
    World w = empty_room();
    add_unit(w, 1, 7, 7);
    add_unit(w, 0, 2, 2);
    step(w, {});
    step(w, {});
    CHECK(w.held[0] == 0);
    CHECK(w.held[1] == 2);
}

TEST_CASE("a contested objective holds for nobody") {
    World w = empty_room();
    add_unit(w, 0, 7, 8);
    add_unit(w, 1, 8, 7);
    step(w, {});
    CHECK(w.held[0] == 0);
    CHECK(w.held[1] == 0);
}

TEST_CASE("held ticks are kept when the side leaves the objective") {
    World w = empty_room();
    add_unit(w, 0, 8, 8);
    step(w, {});
    const std::vector<Command> leave{Command::move(0, 1, 12, 8)};
    step(w, leave);
    for (int i = 0; i < 40; ++i) step(w, {});
    REQUIRE((w.units[0].x.raw >> Fixed::kFracBits) == 12);
    // On the objective for the first tick and for however long the walk off it
    // took; never reset.
    CHECK(w.held[0] >= 2);
    const auto before = w.held[0];
    step(w, {});
    CHECK(w.held[0] == before);
}

TEST_CASE("a match is won by holding the objective long enough or by the last unit standing") {
    World w = empty_room();
    add_unit(w, 0, 2, 2);
    add_unit(w, 1, 12, 12);
    CHECK(winner(w) == -1);

    w.held[1] = kHoldToWin - 1;
    CHECK(winner(w) == -1);
    w.held[1] = kHoldToWin;
    CHECK(winner(w) == 1);

    w.held[1] = 0;
    w.units.pop_back();
    CHECK(winner(w) == 0);
    w.units.clear();
    CHECK(winner(w) == 2);  // both sides gone in the same tick: a draw
}
