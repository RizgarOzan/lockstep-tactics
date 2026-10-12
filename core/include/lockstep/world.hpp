#pragma once
// The simulation's public surface: a world, the inputs one tick consumes, a
// step function and a hash of the full state. Everything the replay tool and
// the CI comparison need goes through these names.
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "lockstep/fixed.hpp"
#include "lockstep/rng.hpp"

namespace lockstep {

enum class Tile : std::uint8_t { Floor = 0, Wall = 1 };

struct Map {
    static constexpr int kWidth = 16;
    static constexpr int kHeight = 16;

    Tile at(int x, int y) const { return tiles[static_cast<std::size_t>(y * kWidth + x)]; }
    void set(int x, int y, Tile t) { tiles[static_cast<std::size_t>(y * kWidth + x)] = t; }

    std::array<Tile, kWidth * kHeight> tiles{};
};

enum class UnitType : std::uint8_t { Soldier = 0, Archer = 1, Knight = 2 };

struct UnitStats {
    std::int32_t max_hp;
    std::int32_t damage;
    Fixed speed;  // tiles per tick
    Fixed range;  // tiles
};

const UnitStats& stats(UnitType type);

enum class Order : std::uint8_t { None = 0, Move = 1, Attack = 2 };

struct Unit {
    std::uint32_t id = 0;
    std::uint8_t owner = 0;
    UnitType type = UnitType::Soldier;
    std::int32_t hp = 0;
    Fixed x;  // tile coordinates; the integer part is the tile
    Fixed y;
    Order order = Order::None;
    std::int32_t target_x = 0;  // Move: destination tile
    std::int32_t target_y = 0;
    std::uint32_t target_id = 0;  // Attack: the unit to hit
    std::uint32_t cooldown = 0;   // ticks until the next attack
};

// Ticks between two attacks of the same unit, hit or miss.
inline constexpr std::uint32_t kAttackCooldown = 10;

// Two players, one unit of each type: make_world() never spawns more, and
// step() sizes its per-tick scratch space from this.
inline constexpr std::size_t kMaxUnits = 6;
// Orders per tick that step() sorts without touching the heap.
inline constexpr std::size_t kInlineCommands = 8;

// One player's order for one unit. Commands for units the player does not
// own, or that no longer exist, are ignored.
struct Command {
    std::uint32_t player = 0;
    std::uint32_t unit_id = 0;
    Order order = Order::None;
    std::int32_t target_x = 0;
    std::int32_t target_y = 0;
    std::uint32_t target_id = 0;

    static Command move(std::uint32_t player, std::uint32_t unit, std::int32_t tx, std::int32_t ty) {
        return {player, unit, Order::Move, tx, ty, 0};
    }
    static Command attack(std::uint32_t player, std::uint32_t unit, std::uint32_t target) {
        return {player, unit, Order::Attack, 0, 0, target};
    }
};

struct World {
    std::uint32_t tick = 0;
    Map map;
    // Kept sorted by id. Iteration order is part of the simulation: a hash
    // container would iterate differently under MSVC and libc++.
    std::vector<Unit> units;
    Pcg32 rng{0u, 0u};
    std::uint32_t hit_percent = 80;
};

// Walls around the edge, random interior walls, both spawn columns clear,
// and one unit of each type per player.
World make_world(std::uint64_t seed);

// One tick: apply this tick's commands (in player, then unit id order,
// whatever order they arrived in), move and attack in unit id order, then
// apply all damage at once and remove the dead.
void step(World& world, std::span<const Command> commands);
std::uint64_t hash(const World& world);

}  // namespace lockstep
