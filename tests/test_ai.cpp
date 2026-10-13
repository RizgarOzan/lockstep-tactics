#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "lockstep/ai.hpp"
#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"

using namespace lockstep;

namespace {

Fixed centre(int tile) { return Fixed::from_int(tile) + Fixed::from_ratio(1, 2); }

World room() {
    World w;
    for (int y = 0; y < Map::kHeight; ++y) {
        for (int x = 0; x < Map::kWidth; ++x) {
            const bool edge = x == 0 || y == 0 || x == Map::kWidth - 1 || y == Map::kHeight - 1;
            w.map.set(x, y, edge ? Tile::Wall : Tile::Floor);
        }
    }
    return w;
}

Unit& add(World& w, std::uint8_t owner, UnitType type, int tx, int ty) {
    const auto id = static_cast<std::uint32_t>(w.units.size() + 1);
    w.units.push_back(Unit{id, owner, type, stats(type).max_hp, centre(tx), centre(ty)});
    return w.units.back();
}

bool same(const std::vector<Command>& a, const std::vector<Command>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].player != b[i].player || a[i].unit_id != b[i].unit_id || a[i].order != b[i].order ||
            a[i].target_x != b[i].target_x || a[i].target_y != b[i].target_y || a[i].target_id != b[i].target_id) {
            return false;
        }
    }
    return true;
}

}  // namespace

TEST_CASE("the AI orders every idle unit to attack, and repeats nothing") {
    const World w = make_world(1);
    const std::vector<Command> orders = ai_orders(w, 1);
    REQUIRE(orders.size() == 3);
    for (const Command& c : orders) {
        CHECK(c.player == 1);
        CHECK(c.order == Order::Attack);
    }
    CHECK(same(ai_orders(w, 1), orders));  // same world, same orders
    World after = w;
    step(after, orders);
    CHECK(ai_orders(after, 1).empty());  // every unit already does what it would be told
}

TEST_CASE("at equal distance the AI picks the matchup it wins") {
    World w = room();
    add(w, 1, UnitType::Soldier, 8, 8);  // id 1, ours
    add(w, 0, UnitType::Knight, 8, 4);   // id 2: 4 tiles up, beats Soldiers
    add(w, 0, UnitType::Archer, 8, 12);  // id 3: 4 tiles down, loses to Soldiers
    const auto orders = ai_orders(w, 1);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].target_id == 3);
}

TEST_CASE("a wounded enemy is preferred over a healthy one") {
    World w = room();
    add(w, 1, UnitType::Knight, 8, 8);
    add(w, 0, UnitType::Knight, 8, 5);
    add(w, 0, UnitType::Knight, 8, 11).hp = 3;
    CHECK(ai_orders(w, 1)[0].target_id == 3);
}

TEST_CASE("an Archer on cooldown steps away from a melee unit next to it") {
    World w = room();
    add(w, 1, UnitType::Archer, 8, 8).cooldown = 5;
    add(w, 0, UnitType::Soldier, 8, 9);
    const auto orders = ai_orders(w, 1);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].order == Order::Move);
    CHECK(orders[0].target_x == 8);
    CHECK(orders[0].target_y == 7);
}

TEST_CASE("the whole side converges on one target") {
    World w = room();
    add(w, 1, UnitType::Soldier, 8, 8);
    add(w, 1, UnitType::Knight, 8, 9);
    add(w, 1, UnitType::Archer, 8, 10);
    add(w, 0, UnitType::Knight, 3, 8).hp = 4;  // wounded, four tiles short of the far one
    add(w, 0, UnitType::Knight, 13, 9);
    const auto orders = ai_orders(w, 1);
    REQUIRE(orders.size() == 3);
    for (const Command& c : orders) {
        CHECK(c.order == Order::Attack);
        CHECK(c.target_id == 4);
    }
}

TEST_CASE("a unit far ahead of its slowest teammate waits, then moves on") {
    World w = room();
    add(w, 1, UnitType::Soldier, 9, 8);  // id 1: close to the enemy
    add(w, 1, UnitType::Knight, 2, 8);   // id 2: seven tiles behind
    add(w, 0, UnitType::Knight, 13, 8);  // id 3: the enemy
    auto orders = ai_orders(w, 1);
    REQUIRE(orders.size() == 2);
    CHECK(orders[0].unit_id == 1);
    CHECK(orders[0].order == Order::Move);  // holds its own tile
    CHECK(orders[0].target_x == 9);
    CHECK(orders[0].target_y == 8);
    CHECK(orders[1].unit_id == 2);
    CHECK(orders[1].order == Order::Attack);
    step(w, orders);
    CHECK(ai_orders(w, 1).empty());  // holding is stable, no order spam
    // Once the Knight is close enough the Soldier is released.
    w.units[1].x = Fixed::from_int(8) + Fixed::from_ratio(1, 2);
    orders = ai_orders(w, 1);
    REQUIRE(orders.size() == 1);
    CHECK(orders[0].unit_id == 1);
    CHECK(orders[0].order == Order::Attack);
}

TEST_CASE("an enemy already in reach is fought rather than walked away from") {
    World w = room();
    add(w, 1, UnitType::Soldier, 8, 8);  // id 1
    add(w, 1, UnitType::Soldier, 8, 9);  // id 2
    add(w, 0, UnitType::Knight, 9, 8);   // id 3: adjacent to id 1
    add(w, 0, UnitType::Archer, 8, 12).hp = 1;  // id 4: the team focus, four tiles off
    const auto orders = ai_orders(w, 1);
    REQUIRE(orders.size() == 2);
    CHECK(orders[0].target_id == 3);  // id 1 hits what is next to it
    CHECK(orders[1].target_id == 4);  // id 2 has nothing in reach and goes for the focus
}

TEST_CASE("the matchup bonus goes one way round the triangle") {
    CHECK(damage(UnitType::Soldier, UnitType::Archer) > stats(UnitType::Soldier).damage);
    CHECK(damage(UnitType::Archer, UnitType::Knight) > stats(UnitType::Archer).damage);
    CHECK(damage(UnitType::Knight, UnitType::Soldier) > stats(UnitType::Knight).damage);
    CHECK(damage(UnitType::Archer, UnitType::Soldier) == stats(UnitType::Archer).damage);
    CHECK(damage(UnitType::Knight, UnitType::Knight) == stats(UnitType::Knight).damage);
}

TEST_CASE("the AI beats the random player in most seeded matches") {
    // Measured at 1,000 seeds with the arena tool; the floor here is lower so
    // a balance tweak does not fail the build over a few seeds.
    int ai_wins = 0;
    const int seeds = 200;
    for (std::uint64_t seed = 0; seed < seeds; ++seed) {
        std::uint64_t rng = seed;
        World w = make_world(seed);
        for (int t = 0; t < 600; ++t) step(w, match_orders(w, rng));
        int blue = 0, orange = 0;
        for (const Unit& u : w.units) (u.owner == 0 ? blue : orange)++;
        ai_wins += orange > blue;
    }
    CHECK(ai_wins >= seeds * 7 / 10);
}
