#include "lockstep/world.hpp"

#include "lockstep/hash.hpp"

namespace lockstep {

namespace {

constexpr std::array<UnitStats, 3> kStats{{
    {10, 3, Fixed::from_ratio(1, 4), Fixed::from_int(1)},  // Soldier
    {6, 2, Fixed::from_ratio(1, 5), Fixed::from_int(4)},   // Archer
    {16, 4, Fixed::from_ratio(1, 6), Fixed::from_int(1)},  // Knight
}};

// Spawn columns stay clear of walls; interior walls only between them.
constexpr int kSpawnColumn[2] = {1, Map::kWidth - 2};
constexpr int kSpawnRow[3] = {5, 7, 9};
constexpr std::uint32_t kWallPercent = 15;

Fixed tile_centre(int tile) { return Fixed::from_int(tile) + Fixed::from_ratio(1, 2); }

}  // namespace

const UnitStats& stats(UnitType type) { return kStats[static_cast<std::size_t>(type)]; }

World make_world(std::uint64_t seed) {
    World world;
    world.rng = Pcg32(seed, 1u);

    for (int y = 0; y < Map::kHeight; ++y) {
        for (int x = 0; x < Map::kWidth; ++x) {
            const bool edge = x == 0 || y == 0 || x == Map::kWidth - 1 || y == Map::kHeight - 1;
            const bool interior = x >= 3 && x <= Map::kWidth - 4;
            // Draw for every interior tile in a fixed order, so the sequence of
            // random numbers consumed never depends on earlier outcomes.
            const bool wall = edge || (interior && world.rng.below(100) < kWallPercent);
            world.map.set(x, y, wall ? Tile::Wall : Tile::Floor);
        }
    }

    std::uint32_t next_id = 1;
    for (std::uint8_t owner = 0; owner < 2; ++owner) {
        for (int t = 0; t < 3; ++t) {
            const auto type = static_cast<UnitType>(t);
            world.units.push_back(Unit{next_id++, owner, type, stats(type).max_hp,
                                       tile_centre(kSpawnColumn[owner]), tile_centre(kSpawnRow[t])});
        }
    }
    return world;
}

void step(World& world, std::span<const Input> inputs) {
    // Placeholder rule until commands exist. Unsigned arithmetic on purpose:
    // signed overflow is undefined behaviour, and an optimiser may treat it
    // differently per compiler, which is exactly a desync.
    auto acc = static_cast<std::uint32_t>(world.placeholder);
    for (const Input& in : inputs) {
        acc = acc * 31u + in.player * 7u + static_cast<std::uint32_t>(in.command);
    }
    world.placeholder = static_cast<std::int32_t>(acc);
    ++world.tick;
}

std::uint64_t hash(const World& world) {
    StateHash h;
    h.add(world.tick);
    for (Tile t : world.map.tiles) {
        h.add(static_cast<std::uint32_t>(t));
    }
    h.add(static_cast<std::uint32_t>(world.units.size()));
    for (const Unit& u : world.units) {
        h.add(u.id);
        h.add(static_cast<std::uint32_t>(u.owner));
        h.add(static_cast<std::uint32_t>(u.type));
        h.add(u.hp);
        h.add(u.x.raw);
        h.add(u.y.raw);
    }
    h.add(world.rng.state());
    h.add(world.rng.increment());
    h.add(world.placeholder);
    return h.value();
}

}  // namespace lockstep
