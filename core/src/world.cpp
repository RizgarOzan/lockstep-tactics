#include "lockstep/world.hpp"

#include <algorithm>
#include <tuple>

#if defined(LOCKSTEP_RISKY_FLOAT)
#include <cmath>
#endif
#if defined(LOCKSTEP_RISKY_UNORDERED)
#include <unordered_map>
#endif

#include "lockstep/fields.hpp"
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

std::int32_t damage(UnitType attacker, UnitType target) {
    // Rock, paper, scissors with a bonus rather than a multiplier: an Archer
    // (6 hp) still survives one Soldier hit, so a mistake costs a unit over a
    // few ticks, not at once.
    constexpr std::int32_t kBonus = 2;
    const bool strong = (attacker == UnitType::Soldier && target == UnitType::Archer) ||
                        (attacker == UnitType::Archer && target == UnitType::Knight) ||
                        (attacker == UnitType::Knight && target == UnitType::Soldier);
    return stats(attacker).damage + (strong ? kBonus : 0);
}

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

namespace {

Unit* find_unit(std::vector<Unit>& units, std::uint32_t id) {
    auto it = std::lower_bound(units.begin(), units.end(), id,
                               [](const Unit& u, std::uint32_t v) { return u.id < v; });
    return it != units.end() && it->id == id ? &*it : nullptr;
}

int tile_of(Fixed v) { return v.raw >> Fixed::kFracBits; }

#if defined(LOCKSTEP_RISKY_FLOAT)
// RISKY BUILD ONLY (cmake -DLOCKSTEP_RISKY=float): moves along the straight
// line with float maths and a square root, the way a game without lockstep
// would. See docs/notes/06-breaking-it-on-purpose.md for what it does.
Fixed to_fixed(float v) { return Fixed::from_raw(static_cast<std::int32_t>(std::lround(v * 65536.0f))); }
float to_float(Fixed v) { return static_cast<float>(v.raw) / 65536.0f; }

void walk(const Map& map, Unit& u, Fixed tx, Fixed ty) {
    const float speed = to_float(stats(u.type).speed);
    float dx = to_float(tx) - to_float(u.x);
    float dy = to_float(ty) - to_float(u.y);
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len > speed) {
        dx = dx / len * speed;
        dy = dy / len * speed;
    }
    const Fixed nx = to_fixed(to_float(u.x) + dx);
    if (map.at(tile_of(nx), tile_of(u.y)) == Tile::Floor) u.x = nx;
    const Fixed ny = to_fixed(to_float(u.y) + dy);
    if (map.at(tile_of(u.x), tile_of(ny)) == Tile::Floor) u.y = ny;
}
#else
// Moves `pos` towards `target` by at most `speed`.
Fixed approach(Fixed pos, Fixed target, Fixed speed) {
    if (target > pos) return target - pos > speed ? pos + speed : target;
    if (target < pos) return pos - target > speed ? pos - speed : target;
    return pos;
}

// One axis at a time, so a wall ahead on x still lets the unit slide on y.
void walk(const Map& map, Unit& u, Fixed tx, Fixed ty) {
    const Fixed speed = stats(u.type).speed;
    const Fixed nx = approach(u.x, tx, speed);
    if (map.at(tile_of(nx), tile_of(u.y)) == Tile::Floor) u.x = nx;
    const Fixed ny = approach(u.y, ty, speed);
    if (map.at(tile_of(u.x), tile_of(ny)) == Tile::Floor) u.y = ny;
}
#endif

struct Hit {
    std::uint32_t target_id;
    std::int32_t damage;
};

}  // namespace

void step(World& world, std::span<const Command> commands) {
    // 1. Orders. Sorted first: two players' packets can arrive in either
    //    order, and the result must not depend on it. The key is every field,
    //    not just (player, unit): with two orders for one unit in one tick, a
    //    stable sort on the unit alone would keep arrival order, and the
    //    "last order wins" would differ between machines.
    std::vector<Command> sorted(commands.begin(), commands.end());
    std::sort(sorted.begin(), sorted.end(), [](const Command& a, const Command& b) {
        return std::tie(a.player, a.unit_id, a.order, a.target_x, a.target_y, a.target_id) <
               std::tie(b.player, b.unit_id, b.order, b.target_x, b.target_y, b.target_id);
    });
    for (const Command& c : sorted) {
        Unit* u = find_unit(world.units, c.unit_id);
        if (u == nullptr || u->owner != c.player) continue;
        u->order = c.order;
        u->target_x = c.target_x;
        u->target_y = c.target_y;
        u->target_id = c.target_id;
    }

    // 2. Movement and attacks, in unit id order. Attacks only record hits.
    std::vector<Unit*> acting;
#if defined(LOCKSTEP_RISKY_UNORDERED)
    // RISKY BUILD ONLY (cmake -DLOCKSTEP_RISKY=unordered): units act in the
    // iteration order of a hash map, which each standard library decides.
    std::unordered_map<std::uint32_t, Unit*> by_id;
    for (Unit& u : world.units) by_id.emplace(u.id, &u);
    for (const auto& entry : by_id) acting.push_back(entry.second);
#else
    for (Unit& u : world.units) acting.push_back(&u);
#endif
    std::vector<Hit> hits;
    for (Unit* unit : acting) {
        Unit& u = *unit;
        if (u.cooldown > 0) --u.cooldown;
        if (u.order == Order::Move) {
            walk(world.map, u, Fixed::from_int(u.target_x) + Fixed::from_ratio(1, 2),
                 Fixed::from_int(u.target_y) + Fixed::from_ratio(1, 2));
        } else if (u.order == Order::Attack) {
            const Unit* target = find_unit(world.units, u.target_id);
            if (target == nullptr) {
                u.order = Order::None;
                continue;
            }
            const Fixed dx = target->x - u.x;
            const Fixed dy = target->y - u.y;
            const Fixed range = stats(u.type).range;
            if (dx * dx + dy * dy > range * range) {
                walk(world.map, u, target->x, target->y);
            } else if (u.cooldown == 0) {
                if (world.rng.below(100) < world.hit_percent) {
                    hits.push_back({target->id, damage(u.type, target->type)});
                }
                u.cooldown = kAttackCooldown;
            }
        }
    }

    // 3. Damage lands all at once, so a unit killed this tick still struck
    //    back, whichever of the two has the lower id.
    for (const Hit& h : hits) {
        if (Unit* t = find_unit(world.units, h.target_id)) t->hp -= h.damage;
    }
    std::erase_if(world.units, [](const Unit& u) { return u.hp <= 0; });
    ++world.tick;
}

std::uint64_t hash(const World& world) {
    // Every field, as 8 little-endian bytes each, in visit_fields() order:
    // the desync tools read the same list, so the two cannot drift apart.
    StateHash h;
    visit_fields(world, [&](const Field&, std::uint64_t value) { h.add(value); });
#if defined(LOCKSTEP_RISKY_SIZE_T)
    // RISKY BUILD ONLY (cmake -DLOCKSTEP_RISKY=size_t): also feed the unit
    // count as a size_t, one byte per byte of *this target's* size_t. That is
    // 8 bytes on x64 and 4 on wasm32, so the two can never agree, on any
    // tick, however correct the simulation is. See docs/notes/06.
    const std::size_t n = world.units.size();
    for (std::size_t i = 0; i < sizeof n; ++i) {
        h.add(static_cast<std::uint32_t>((n >> (8 * i)) & 0xffu));
    }
#endif
    return h.value();
}

}  // namespace lockstep
