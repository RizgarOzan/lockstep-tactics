#include <doctest/doctest.h>

#include <array>
#include <functional>
#include <vector>

#include "lockstep/world.hpp"

using namespace lockstep;

TEST_CASE("the same seed builds the same world, another seed another one") {
    CHECK(hash(make_world(1)) == hash(make_world(1)));
    CHECK(hash(make_world(1)) != hash(make_world(2)));
}

TEST_CASE("the map is closed by walls and the spawn columns are clear") {
    const World w = make_world(3);
    for (int x = 0; x < Map::kWidth; ++x) {
        CHECK(w.map.at(x, 0) == Tile::Wall);
        CHECK(w.map.at(x, Map::kHeight - 1) == Tile::Wall);
    }
    for (int y = 1; y < Map::kHeight - 1; ++y) {
        CHECK(w.map.at(0, y) == Tile::Wall);
        CHECK(w.map.at(Map::kWidth - 1, y) == Tile::Wall);
        for (int x : {1, 2, Map::kWidth - 3, Map::kWidth - 2}) {
            CHECK(w.map.at(x, y) == Tile::Floor);
        }
    }
}

TEST_CASE("each player gets one unit of each type, at full health, on floor") {
    const World w = make_world(4);
    REQUIRE(w.units.size() == 6);
    std::array<std::array<int, 3>, 2> seen{};
    for (std::size_t i = 0; i < w.units.size(); ++i) {
        const Unit& u = w.units[i];
        CHECK(u.id == i + 1);  // ascending ids: iteration order is the id order
        CHECK(u.hp == stats(u.type).max_hp);
        CHECK(w.map.at(u.x.raw >> Fixed::kFracBits, u.y.raw >> Fixed::kFracBits) == Tile::Floor);
        ++seen[u.owner][static_cast<int>(u.type)];
    }
    for (const auto& player : seen) {
        CHECK(player == std::array<int, 3>{1, 1, 1});
    }
}

TEST_CASE("the hash covers every field of the state") {
    const World base = make_world(5);
    const std::uint64_t h = hash(base);
    const std::vector<std::function<void(World&)>> edits{
        [](World& w) { ++w.tick; },
        [](World& w) { w.map.set(5, 5, w.map.at(5, 5) == Tile::Wall ? Tile::Floor : Tile::Wall); },
        [](World& w) { w.units.pop_back(); },
        [](World& w) { ++w.units[0].id; },
        [](World& w) { w.units[0].owner ^= 1u; },
        [](World& w) { w.units[0].type = UnitType::Knight == w.units[0].type ? UnitType::Soldier : UnitType::Knight; },
        [](World& w) { --w.units[0].hp; },
        [](World& w) { w.units[0].x = w.units[0].x + Fixed::from_raw(1); },
        [](World& w) { w.units[0].y = w.units[0].y + Fixed::from_raw(1); },
        [](World& w) { w.rng.next(); },
        [](World& w) { ++w.placeholder; },
    };
    for (std::size_t i = 0; i < edits.size(); ++i) {
        World changed = base;
        edits[i](changed);
        CAPTURE(i);
        CHECK(hash(changed) != h);
    }
}
