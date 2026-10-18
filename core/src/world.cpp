#include "lockstep/world.hpp"

#include <algorithm>
#include <array>
#include <span>
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
// Top-left tile of the 2x2 objective.
constexpr int kObjectiveX = Map::kWidth / 2 - 1;
constexpr int kObjectiveY = Map::kHeight / 2 - 1;

Fixed tile_centre(int tile) { return Fixed::from_int(tile) + Fixed::from_ratio(1, 2); }

}  // namespace

const UnitStats& stats(UnitType type) { return kStats[static_cast<std::size_t>(type)]; }

bool on_objective(int tx, int ty) {
    return tx >= kObjectiveX && tx < kObjectiveX + 2 && ty >= kObjectiveY && ty < kObjectiveY + 2;
}

int winner(const World& world) {
    if (world.held[0] >= kHoldToWin) return 0;
    if (world.held[1] >= kHoldToWin) return 1;
    bool alive[2] = {false, false};
    for (const Unit& u : world.units) alive[u.owner] = true;
    if (alive[0] && alive[1]) return -1;
    return alive[0] ? 0 : alive[1] ? 1 : 2;
}

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

    // The objective is always floor, drawn walls or not. Walling all of it in
    // takes a ring of at least eight walls; 100,000 seeds never drew one
    // (docs/notes/14), and if one ever does, the fill below walls the
    // objective too and that map is won by fighting alone.
    for (int y = kObjectiveY; y < kObjectiveY + 2; ++y) {
        for (int x = kObjectiveX; x < kObjectiveX + 2; ++x) world.map.set(x, y, Tile::Floor);
    }

    // Fill every floor tile the spawns cannot reach, so no unit can be sent
    // into a pocket and no target can hide in one. A flood fill, no random
    // draws, so the one-draw-per-tile rule above still holds.
    std::array<bool, Map::kWidth * Map::kHeight> seen{};
    std::array<std::int16_t, Map::kWidth * Map::kHeight> queue{};
    std::size_t head = 0, tail = 0;
    seen[static_cast<std::size_t>(kSpawnRow[0] * Map::kWidth + kSpawnColumn[0])] = true;
    queue[tail++] = static_cast<std::int16_t>(kSpawnRow[0] * Map::kWidth + kSpawnColumn[0]);
    while (head < tail) {
        const int at = queue[head++];
        const int x = at % Map::kWidth, y = at / Map::kWidth;
        constexpr int kDx[4] = {0, -1, 1, 0}, kDy[4] = {-1, 0, 0, 1};
        for (int n = 0; n < 4; ++n) {
            const int nx = x + kDx[n], ny = y + kDy[n];
            if (world.map.at(nx, ny) != Tile::Floor) continue;
            auto& s = seen[static_cast<std::size_t>(ny * Map::kWidth + nx)];
            if (s) continue;
            s = true;
            queue[tail++] = static_cast<std::int16_t>(ny * Map::kWidth + nx);
        }
    }
    for (int y = 0; y < Map::kHeight; ++y) {
        for (int x = 0; x < Map::kWidth; ++x) {
            if (!seen[static_cast<std::size_t>(y * Map::kWidth + x)]) world.map.set(x, y, Tile::Wall);
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

std::optional<std::pair<int, int>> next_step(const Map& map, int sx, int sy, int gx, int gy) {
    if (sx == gx && sy == gy) return std::nullopt;
    if (map.at(gx, gy) != Tile::Floor) return std::nullopt;
    // A* from the start with the Manhattan distance as the estimate, which
    // never overestimates on a four-connected grid, so the first time the
    // goal is taken off the open list its path is a shortest one. The open
    // list is a set of buckets by f = g + h, each a FIFO; a tile with the
    // same f but a smaller h goes in front of its bucket. That fixes which
    // of several shortest paths is taken: the one that heads for the goal
    // first, and among equals the one found first in the neighbour order
    // up, left, right, down. No heap, no float, the same on every machine.
    constexpr int kN = Map::kWidth * Map::kHeight;
    constexpr int kMaxF = 2 * (Map::kWidth + Map::kHeight);
    constexpr std::int16_t kNone = -1;
    std::array<std::int16_t, kN> g;
    g.fill(kNone);
    std::array<std::int16_t, kN> parent;
    // Written before read, so left uninitialised on purpose: zeroing them
    // was a third of the search's cost.
    std::array<std::int16_t, kN * 2> bucket;  // every (tile, f) pair pushed at most a few times
    std::array<std::int16_t, kN * 2> link;    // next index in the same bucket
    std::array<std::int16_t, kMaxF + 1> bucket_head{}, bucket_tail{};
    int pushed = 0;
    const auto index = [](int x, int y) { return y * Map::kWidth + x; };
    const auto h = [&](int x, int y) { return (x > gx ? x - gx : gx - x) + (y > gy ? y - gy : gy - y); };
    const auto push = [&](int tile, int f, bool front) {
        const int slot = pushed++;
        bucket[static_cast<std::size_t>(slot)] = static_cast<std::int16_t>(tile);
        auto& head = bucket_head[static_cast<std::size_t>(f)];
        auto& tail = bucket_tail[static_cast<std::size_t>(f)];
        if (head == 0) {  // empty: slots are stored +1 so 0 means none
            head = tail = static_cast<std::int16_t>(slot + 1);
            link[static_cast<std::size_t>(slot)] = 0;
        } else if (front) {
            link[static_cast<std::size_t>(slot)] = head;
            head = static_cast<std::int16_t>(slot + 1);
        } else {
            link[static_cast<std::size_t>(tail - 1)] = static_cast<std::int16_t>(slot + 1);
            link[static_cast<std::size_t>(slot)] = 0;
            tail = static_cast<std::int16_t>(slot + 1);
        }
    };
    const int start = index(sx, sy), goal = index(gx, gy);
    g[static_cast<std::size_t>(start)] = 0;
    parent[static_cast<std::size_t>(start)] = kNone;
    push(start, h(sx, sy), false);
    constexpr int kDx[4] = {0, -1, 1, 0};
    constexpr int kDy[4] = {-1, 0, 0, 1};
    for (int f = 0; f <= kMaxF; ++f) {
        while (bucket_head[static_cast<std::size_t>(f)] != 0) {
            const int slot = bucket_head[static_cast<std::size_t>(f)] - 1;
            bucket_head[static_cast<std::size_t>(f)] = link[static_cast<std::size_t>(slot)];
            const int at = bucket[static_cast<std::size_t>(slot)];
            const int x = at % Map::kWidth, y = at / Map::kWidth;
            const int ga = g[static_cast<std::size_t>(at)];
            if (ga + h(x, y) != f) continue;  // a stale entry: the tile was reached cheaper later
            if (at == goal) {
                int step = at;
                while (parent[static_cast<std::size_t>(step)] != start) step = parent[static_cast<std::size_t>(step)];
                return std::make_pair(step % Map::kWidth, step / Map::kWidth);
            }
            for (int n = 0; n < 4; ++n) {
                const int nx = x + kDx[n], ny = y + kDy[n];
                if (nx < 0 || ny < 0 || nx >= Map::kWidth || ny >= Map::kHeight) continue;
                if (map.at(nx, ny) != Tile::Floor) continue;
                const int ni = index(nx, ny);
                auto& gn = g[static_cast<std::size_t>(ni)];
                if (gn != kNone && gn <= ga + 1) continue;
                gn = static_cast<std::int16_t>(ga + 1);
                parent[static_cast<std::size_t>(ni)] = static_cast<std::int16_t>(at);
                const int hn = h(nx, ny);
                const int fn = ga + 1 + hn;
                if (fn > kMaxF || pushed >= kN * 2) return std::nullopt;  // cannot happen on this grid
                // Same f, smaller h: in front of the bucket, so the search keeps
                // heading for the goal; same h: behind, in neighbour order.
                push(ni, fn, hn < h(x, y));
            }
        }
    }
    return std::nullopt;
}

bool line_of_sight(const Map& map, int x0, int y0, int x1, int y1) {
    // Integer Bresenham: the same tiles on every machine, no float, and
    // symmetric enough for a game (a line is checked from the attacker's end).
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    while (true) {
        if (x == x1 && y == y1) return true;
        const int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
        if ((x != x1 || y != y1) && map.at(x, y) == Tile::Wall) return false;
    }
}

bool line_clear(const Map& map, int x0, int y0, int x1, int y1) {
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
    const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    while (!(x == x1 && y == y1)) {
        const int e2 = 2 * err;
        const bool step_x = e2 > -dy, step_y = e2 < dx;
        if (step_x && step_y) {
            // A diagonal step touches the two tiles beside the corner as well.
            if (map.at(x + sx, y) != Tile::Floor || map.at(x, y + sy) != Tile::Floor) return false;
        }
        if (step_x) {
            err -= dy;
            x += sx;
        }
        if (step_y) {
            err += dx;
            y += sy;
        }
        if ((x != x1 || y != y1) && map.at(x, y) != Tile::Floor) return false;
    }
    return true;
}

namespace {

Unit* find_unit(std::vector<Unit>& units, std::uint32_t id) {
    auto it = std::lower_bound(units.begin(), units.end(), id,
                               [](const Unit& u, std::uint32_t v) { return u.id < v; });
    return it != units.end() && it->id == id ? &*it : nullptr;
}

const Unit* find_unit(std::span<const Unit> units, std::uint32_t id) {
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

void stride(const Map& map, Unit& u, Fixed tx, Fixed ty) {
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
Fixed abs(Fixed v) { return v.raw < 0 ? Fixed::from_raw(-v.raw) : v; }

// One tick of movement along the segment to (tx, ty): the longer axis moves
// by `speed`, the shorter by the matching fraction, so the unit stays on
// the line that line_clear() checked (a move per axis of up to `speed`
// each would cut 45 degree corners off it and leave the checked corridor).
// The axes are still applied one at a time, so a wall ahead on x lets the
// unit slide on y.
void stride(const Map& map, Unit& u, Fixed tx, Fixed ty) {
    const Fixed speed = stats(u.type).speed;
    const Fixed dx = tx - u.x, dy = ty - u.y;
    const Fixed ax = abs(dx), ay = abs(dy);
    Fixed sx = dx, sy = dy;  // the whole way, when it is within one stride
    const Fixed longest = ax > ay ? ax : ay;
    if (longest > speed) {
        if (ax >= ay) {
            sx = dx.raw < 0 ? Fixed::from_raw(-speed.raw) : speed;
            sy = speed * dy / ax;
        } else {
            sy = dy.raw < 0 ? Fixed::from_raw(-speed.raw) : speed;
            sx = speed * dx / ay;
        }
    }
    const Fixed nx = u.x + sx;
    if (map.at(tile_of(nx), tile_of(u.y)) == Tile::Floor) u.x = nx;
    const Fixed ny = u.y + sy;
    if (map.at(tile_of(u.x), tile_of(ny)) == Tile::Floor) u.y = ny;
}
#endif

// Where `u` would be after one tick of walking towards (tx, ty), given the
// map and where everyone else stood at the start of the tick. Other units'
// tiles count as walls, except the goal tile, so an attacker still closes in
// on its target; it stops short of entering a goal tile someone else stands
// on. If the straight segment to the goal is clear the unit walks it, corners
// and all, with no search; otherwise it strides to the first tile of the
// shortest four-connected path. A goal that cannot be reached falls back to
// the straight line, which at worst stands still.
std::pair<Fixed, Fixed> plan_walk(const Map& map, std::span<const Unit> others, const Unit& u, Fixed tx, Fixed ty) {
    const int sx = tile_of(u.x), sy = tile_of(u.y), gx = tile_of(tx), gy = tile_of(ty);
    Map blocked = map;
    bool goal_taken = false;
    for (const Unit& o : others) {
        if (o.id == u.id) continue;
        const int ox = tile_of(o.x), oy = tile_of(o.y);
        if (ox == gx && oy == gy) goal_taken = true;
        else blocked.set(ox, oy, Tile::Wall);
    }
    if (!line_clear(blocked, sx, sy, gx, gy)) {
        if (const auto next = next_step(blocked, sx, sy, gx, gy)) {
            tx = tile_centre(next->first);
            ty = tile_centre(next->second);
        }
    }
    if (goal_taken && (sx != gx || sy != gy)) blocked.set(gx, gy, Tile::Wall);
    Unit moved = u;
    stride(blocked, moved, tx, ty);
    return {moved.x, moved.y};
}

struct Hit {
    std::uint32_t target_id;
    std::int32_t damage;
};

// The acting loop walks either the unit vector or, in the risky build, a list
// of pointers into it.
[[maybe_unused]] Unit& deref(Unit& u) { return u; }
[[maybe_unused]] Unit& deref(Unit* u) { return *u; }

}  // namespace

void step(World& world, std::span<const Command> commands) {
    // 1. Orders. Sorted first: two players' packets can arrive in either
    //    order, and the result must not depend on it. The key is every field,
    //    not just (player, unit): with two orders for one unit in one tick, a
    //    stable sort on the unit alone would keep arrival order, and the
    //    "last order wins" would differ between machines.
    //    A tick rarely carries more than a handful of orders, so they are
    //    sorted in a buffer on the stack; only a flood of orders pays for a
    //    heap allocation. Measured in docs/notes/11-benchmark.md.
    std::array<Command, kInlineCommands> inline_commands;
    std::vector<Command> spilled_commands;
    std::span<Command> sorted;
    if (commands.size() <= inline_commands.size()) {
        std::copy(commands.begin(), commands.end(), inline_commands.begin());
        sorted = std::span<Command>(inline_commands.data(), commands.size());
    } else {
        spilled_commands.assign(commands.begin(), commands.end());
        sorted = spilled_commands;
    }
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
    //    Nothing is added to or removed from `units` until step 3, so the
    //    vector is walked in place; a unit lands at most one hit per tick, so
    //    the hits fit in a fixed array of kMaxUnits.
#if defined(LOCKSTEP_RISKY_UNORDERED)
    // RISKY BUILD ONLY (cmake -DLOCKSTEP_RISKY=unordered): units act in the
    // iteration order of a hash map, which each standard library decides.
    std::unordered_map<std::uint32_t, Unit*> by_id;
    for (Unit& u : world.units) by_id.emplace(u.id, &u);
    std::vector<Unit*> acting;
    for (const auto& entry : by_id) acting.push_back(entry.second);
#else
    std::vector<Unit>& acting = world.units;
#endif
    //    Every decision reads `before`, the units as they stood when the tick
    //    began, so a unit that acts later in the loop does not see the moves
    //    of the ones before it: with the same AI on both sides the lower ids
    //    used to win noticeably more, and this is why. Moves are planned
    //    here and applied below.
    std::array<Unit, kMaxUnits> before_storage;
    std::copy(world.units.begin(), world.units.end(), before_storage.begin());
    const std::span<const Unit> before(before_storage.data(), world.units.size());
    std::array<Hit, kMaxUnits> hits;
    std::size_t hit_count = 0;
    std::array<std::pair<Fixed, Fixed>, kMaxUnits> planned;
    std::array<bool, kMaxUnits> moves{};
    std::size_t slot = 0;
    for (auto& unit : acting) {
        Unit& u = deref(unit);
        const std::size_t i = slot++;
        if (u.cooldown > 0) --u.cooldown;
        const Unit& was = *find_unit(before, u.id);
        if (u.order == Order::Move) {
            planned[i] = plan_walk(world.map, before, was, Fixed::from_int(u.target_x) + Fixed::from_ratio(1, 2),
                                   Fixed::from_int(u.target_y) + Fixed::from_ratio(1, 2));
            moves[i] = true;
        } else if (u.order == Order::Attack) {
            const Unit* target = find_unit(before, u.target_id);
            if (target == nullptr) {
                u.order = Order::None;
                continue;
            }
            const Fixed dx = target->x - was.x;
            const Fixed dy = target->y - was.y;
            const Fixed range = stats(u.type).range;
            const bool in_range = dx * dx + dy * dy <= range * range &&
                                  line_of_sight(world.map, tile_of(was.x), tile_of(was.y), tile_of(target->x), tile_of(target->y));
            if (!in_range) {
                planned[i] = plan_walk(world.map, before, was, target->x, target->y);
                moves[i] = true;
            } else if (u.cooldown == 0) {
                if (world.rng.below(100) < world.hit_percent) {
                    hits[hit_count++] = {target->id, damage(u.type, target->type)};
                }
                u.cooldown = kAttackCooldown;
            }
        }
    }

    // 2b. Moves land in id order. A tile that another unit already holds, at
    //     its new position if it moved first and its old one otherwise, is
    //     refused and the unit stays; so two units walking into one tile is
    //     settled by id, and nothing else in the tick is.
    slot = 0;
    for (auto& unit : acting) {
        Unit& u = deref(unit);
        const std::size_t i = slot++;
        if (!moves[i]) continue;
        const int nx = tile_of(planned[i].first), ny = tile_of(planned[i].second);
        bool taken = false;
        if (nx != tile_of(u.x) || ny != tile_of(u.y)) {
            for (const Unit& o : world.units) {
                if (o.id != u.id && tile_of(o.x) == nx && tile_of(o.y) == ny) taken = true;
            }
        }
        if (!taken) {
            u.x = planned[i].first;
            u.y = planned[i].second;
        }
    }

    // 3. Damage lands all at once, so a unit killed this tick still struck
    //    back, whichever of the two has the lower id.
    for (std::size_t i = 0; i < hit_count; ++i) {
        if (Unit* t = find_unit(world.units, hits[i].target_id)) t->hp -= hits[i].damage;
    }
    std::erase_if(world.units, [](const Unit& u) { return u.hp <= 0; });

    // 4. The objective, from where the survivors stand: held by a side only
    //    while the other has nobody on it.
    bool there[2] = {false, false};
    for (const Unit& u : world.units) {
        if (on_objective(tile_of(u.x), tile_of(u.y))) there[u.owner] = true;
    }
    if (there[0] != there[1]) ++world.held[there[0] ? 0 : 1];
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
