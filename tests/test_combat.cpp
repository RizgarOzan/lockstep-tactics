#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "lockstep/world.hpp"

using namespace lockstep;

namespace {

// An open 16x16 room with walls only on the edge and no units.
World empty_room() {
    World w;
    for (int y = 0; y < Map::kHeight; ++y) {
        for (int x = 0; x < Map::kWidth; ++x) {
            const bool edge = x == 0 || y == 0 || x == Map::kWidth - 1 || y == Map::kHeight - 1;
            w.map.set(x, y, edge ? Tile::Wall : Tile::Floor);
        }
    }
    w.hit_percent = 100;
    return w;
}

Fixed centre(int tile) { return Fixed::from_int(tile) + Fixed::from_ratio(1, 2); }

Unit& add_unit(World& w, std::uint8_t owner, UnitType type, int tx, int ty) {
    const auto id = static_cast<std::uint32_t>(w.units.size() + 1);
    w.units.push_back(Unit{id, owner, type, stats(type).max_hp, centre(tx), centre(ty)});
    return w.units.back();
}

void run(World& w, std::vector<Command> commands, int ticks) {
    step(w, commands);
    for (int i = 1; i < ticks; ++i) {
        step(w, {});
    }
}

const Unit* find(const World& w, std::uint32_t id) {
    for (const Unit& u : w.units) {
        if (u.id == id) return &u;
    }
    return nullptr;
}

}  // namespace

TEST_CASE("a move order walks at the unit's speed and stops on the tile centre") {
    World w = empty_room();
    add_unit(w, 0, UnitType::Soldier, 2, 2);  // speed 1/4 tile per tick
    run(w, {Command::move(0, 1, 5, 2)}, 1);
    CHECK(w.units[0].x == centre(2) + Fixed::from_ratio(1, 4));
    run(w, {}, 20);
    CHECK(w.units[0].x == centre(5));
    CHECK(w.units[0].y == centre(2));
}

TEST_CASE("a wall in the way is walked around, not leaned on") {
    World w = empty_room();
    for (int y = 1; y <= 3; ++y) w.map.set(4, y, Tile::Wall);  // a bar across rows 1..3
    add_unit(w, 0, UnitType::Soldier, 2, 2);
    run(w, {Command::move(0, 1, 6, 2)}, 80);
    CHECK((w.units[0].x.raw >> Fixed::kFracBits) == 6);
    CHECK((w.units[0].y.raw >> Fixed::kFracBits) == 2);
}

TEST_CASE("next_step takes the shortest way round and refuses the unreachable") {
    World w = empty_room();
    for (int y = 1; y <= 3; ++y) w.map.set(4, y, Tile::Wall);
    const auto s = next_step(w.map, 2, 2, 6, 2);
    REQUIRE(s.has_value());
    // Right and down are both 7 steps from the goal; the fixed order picks right.
    CHECK(s->first == 3);
    CHECK(s->second == 2);
    CHECK_FALSE(next_step(w.map, 2, 2, 2, 2).has_value());  // already there
    CHECK_FALSE(next_step(w.map, 2, 2, 4, 2).has_value());  // into a wall
    // A pocket: floor tile (8,8) boxed in by walls.
    for (int d : {-1, 1}) {
        w.map.set(8 + d, 8, Tile::Wall);
        w.map.set(8, 8 + d, Tile::Wall);
    }
    CHECK_FALSE(next_step(w.map, 2, 2, 8, 8).has_value());
}

TEST_CASE("a wall between archer and target is cover: no shot, the archer walks round") {
    World w = empty_room();
    for (int y = 1; y <= 6; ++y) w.map.set(7, y, Tile::Wall);
    add_unit(w, 0, UnitType::Archer, 5, 3);  // range 4: (9,3) is in range by distance alone
    add_unit(w, 1, UnitType::Knight, 9, 3);
    run(w, {Command::attack(0, 1, 2)}, 1);
    CHECK(find(w, 2)->hp == stats(UnitType::Knight).max_hp);
    // It started walking round (right or down are equally short), not standing still.
    CHECK((w.units[0].x > centre(5) || w.units[0].y > centre(3)));
    CHECK_FALSE(line_of_sight(w.map, 5, 3, 9, 3));
    CHECK(line_of_sight(w.map, 5, 3, 5, 8));
    CHECK(line_of_sight(w.map, 8, 8, 9, 3));
}

TEST_CASE("two units never share a tile: the one behind waits or goes round") {
    World w = empty_room();
    for (int y = 1; y <= 14; ++y) {
        if (y != 5) w.map.set(6, y, Tile::Wall);  // a wall with one gap at (6,5)
    }
    add_unit(w, 0, UnitType::Knight, 5, 5);   // id 1, slow, parked in front of the gap
    add_unit(w, 0, UnitType::Soldier, 3, 5);  // id 2, fast, wants through
    run(w, {Command::move(0, 2, 6, 5)}, 40);
    // The Knight has no order and stays at (5,5); the Soldier cannot pass it.
    const Unit* s = find(w, 2);
    REQUIRE(s != nullptr);
    CHECK((s->x.raw >> Fixed::kFracBits) <= 4);
    // Give the Knight room and the Soldier gets through.
    run(w, {Command::move(0, 1, 9, 5)}, 60);
    s = find(w, 2);
    CHECK((s->x.raw >> Fixed::kFracBits) == 6);
    for (const Unit& a : w.units) {
        for (const Unit& b : w.units) {
            if (a.id < b.id) {
                CHECK(((a.x.raw >> Fixed::kFracBits) != (b.x.raw >> Fixed::kFracBits) ||
                       (a.y.raw >> Fixed::kFracBits) != (b.y.raw >> Fixed::kFracBits)));
            }
        }
    }
}

TEST_CASE("an attacker walks round a wall to reach its target") {
    World w = empty_room();
    for (int y = 1; y <= 6; ++y) w.map.set(7, y, Tile::Wall);
    add_unit(w, 0, UnitType::Soldier, 5, 3);
    add_unit(w, 1, UnitType::Knight, 9, 3);
    run(w, {Command::attack(0, 1, 2)}, 60);  // 11 tiles round the wall at 1/4 tile per tick
    REQUIRE(find(w, 2) != nullptr);
    CHECK(find(w, 2)->hp < stats(UnitType::Knight).max_hp);
}

TEST_CASE("an attack in range deals the attacker's damage, then waits for its cooldown") {
    World w = empty_room();
    add_unit(w, 0, UnitType::Soldier, 4, 4);
    add_unit(w, 1, UnitType::Knight, 5, 4);
    const std::int32_t after_one_hit = stats(UnitType::Knight).max_hp - stats(UnitType::Soldier).damage;
    run(w, {Command::attack(0, 1, 2)}, 1);
    REQUIRE(find(w, 2) != nullptr);
    CHECK(find(w, 2)->hp == after_one_hit);
    run(w, {}, kAttackCooldown - 1);
    REQUIRE(find(w, 2) != nullptr);
    CHECK(find(w, 2)->hp == after_one_hit);
    run(w, {}, 1);
    REQUIRE(find(w, 2) != nullptr);
    CHECK(find(w, 2)->hp == after_one_hit - stats(UnitType::Soldier).damage);
}

TEST_CASE("out of range, the attacker walks towards its target") {
    World w = empty_room();
    add_unit(w, 0, UnitType::Soldier, 2, 4);
    add_unit(w, 1, UnitType::Knight, 9, 4);
    const Fixed before = w.units[0].x;
    run(w, {Command::attack(0, 1, 2)}, 1);
    CHECK(w.units[0].x > before);
}

TEST_CASE("acting first in a tick buys nothing: two equal units meet on the same tick") {
    // Blue id 1 and Orange id 2 are both Soldiers, three tiles apart, each
    // ordered at the other. Before the fix id 1 moved first and id 2 then
    // measured range against the new position, so one side struck a tick
    // early. Both must take their first hit on the same tick.
    World w = empty_room();
    add_unit(w, 0, UnitType::Soldier, 4, 6);
    add_unit(w, 1, UnitType::Soldier, 9, 6);
    run(w, {Command::attack(0, 1, 2), Command::attack(1, 2, 1)}, 1);
    for (int t = 0; t < 60; ++t) {
        const std::int32_t a = find(w, 1)->hp, b = find(w, 2)->hp;
        CHECK(a == b);
        if (a < stats(UnitType::Soldier).max_hp) break;
        step(w, {});
    }
    CHECK(find(w, 1)->hp < stats(UnitType::Soldier).max_hp);
}

TEST_CASE("a clear straight line is walked straight, diagonally") {
    World w = empty_room();
    add_unit(w, 0, UnitType::Soldier, 2, 2);
    run(w, {Command::move(0, 1, 6, 6)}, 2);
    // Both axes moved: a four-connected path would have moved one at a time.
    CHECK(w.units[0].x > centre(2));
    CHECK(w.units[0].y > centre(2));
    CHECK(line_clear(w.map, 2, 2, 6, 6));
    w.map.set(4, 4, Tile::Wall);
    CHECK_FALSE(line_clear(w.map, 2, 2, 6, 6));
    // A diagonal squeeze between two walls is not clear either.
    World c = empty_room();
    c.map.set(3, 2, Tile::Wall);
    c.map.set(2, 3, Tile::Wall);
    CHECK_FALSE(line_clear(c.map, 2, 2, 4, 4));
    CHECK(line_of_sight(c.map, 2, 2, 4, 4));  // but an arrow still passes the corner
}

TEST_CASE("damage is simultaneous: two units that kill each other both die") {
    World w = empty_room();
    add_unit(w, 0, UnitType::Soldier, 4, 4).hp = 1;
    add_unit(w, 1, UnitType::Soldier, 5, 4).hp = 1;
    run(w, {Command::attack(0, 1, 2), Command::attack(1, 2, 1)}, 1);
    CHECK(w.units.empty());
}

TEST_CASE("orders for another player's unit, or a dead one, are ignored") {
    World w = empty_room();
    add_unit(w, 0, UnitType::Soldier, 2, 2);
    run(w, {Command::move(1, 1, 8, 2)}, 5);
    CHECK(w.units[0].x == centre(2));
    run(w, {Command::move(0, 99, 8, 2)}, 1);  // no unit 99
    CHECK(w.units[0].x == centre(2));
}

TEST_CASE("the order commands arrive in does not matter") {
    World a = make_world(9), b = make_world(9);
    // Two orders for unit 1 in one tick: only a total order on commands makes
    // "the last one wins" the same on every machine.
    std::vector<Command> commands{Command::move(0, 1, 6, 6), Command::attack(1, 4, 2),
                                  Command::move(0, 1, 2, 12), Command::move(1, 6, 10, 3)};
    step(a, commands);
    std::reverse(commands.begin(), commands.end());
    step(b, commands);
    for (int i = 0; i < 50; ++i) {
        step(a, {});
        step(b, {});
    }
    CHECK(hash(a) == hash(b));
}

TEST_CASE("misses are drawn from the world RNG, so they replay identically") {
    World a = empty_room();
    a.hit_percent = 50;
    add_unit(a, 0, UnitType::Archer, 3, 4);
    add_unit(a, 1, UnitType::Knight, 6, 4);
    World b = a;
    run(a, {Command::attack(0, 1, 2)}, 200);
    run(b, {Command::attack(0, 1, 2)}, 200);
    CHECK(hash(a) == hash(b));
}
