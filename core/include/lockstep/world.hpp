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

struct Unit {
    std::uint32_t id = 0;
    std::uint8_t owner = 0;
    UnitType type = UnitType::Soldier;
    std::int32_t hp = 0;
    Fixed x;  // tile coordinates; the integer part is the tile
    Fixed y;
};

struct Input {
    std::uint32_t player = 0;
    std::int32_t command = 0;
};

struct World {
    std::uint32_t tick = 0;
    Map map;
    // Kept sorted by id. Iteration order is part of the simulation: a hash
    // container would iterate differently under MSVC and libc++.
    std::vector<Unit> units;
    Pcg32 rng{0u, 0u};
    // Mixes the inputs until commands exist (see docs/notes/03-map-and-units.md).
    std::int32_t placeholder = 0;
};

// Walls around the edge, random interior walls, both spawn columns clear,
// and one unit of each type per player.
World make_world(std::uint64_t seed);

void step(World& world, std::span<const Input> inputs);
std::uint64_t hash(const World& world);

}  // namespace lockstep
