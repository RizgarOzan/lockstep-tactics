#pragma once
// The one list of every field in the simulation state. hash(), dump() and
// first_difference() all walk it, so a field added to the hash cannot be
// forgotten by the desync tools, or the other way round.
#include <cstdint>
#include <string>

#include "lockstep/world.hpp"

namespace lockstep {

struct Field {
    const char* name;
    std::int64_t a = -1;  // map: x; unit fields: unit id
    std::int64_t b = -1;  // map: y
};

// "tick", "map[3,5]", "unit#4.hp".
std::string describe(const Field& f);

namespace detail {
inline std::uint64_t bits(std::int32_t v) { return static_cast<std::uint32_t>(v); }
inline std::uint64_t bits(std::uint32_t v) { return v; }
inline std::uint64_t bits(std::uint64_t v) { return v; }
}  // namespace detail

// Calls f(Field, std::uint64_t value) for every field, in a fixed order.
// Signed values are passed as their 32-bit two's complement pattern.
template <class F>
void visit_fields(const World& w, F&& f) {
    using detail::bits;
    f(Field{"tick"}, bits(w.tick));
    for (int y = 0; y < Map::kHeight; ++y) {
        for (int x = 0; x < Map::kWidth; ++x) {
            f(Field{"map", x, y}, static_cast<std::uint64_t>(w.map.at(x, y)));
        }
    }
    f(Field{"units.size"}, static_cast<std::uint64_t>(w.units.size()));
    for (const Unit& u : w.units) {
        const std::int64_t id = u.id;
        f(Field{"unit.id", id}, bits(u.id));
        f(Field{"unit.owner", id}, static_cast<std::uint64_t>(u.owner));
        f(Field{"unit.type", id}, static_cast<std::uint64_t>(u.type));
        f(Field{"unit.hp", id}, bits(u.hp));
        f(Field{"unit.x", id}, bits(u.x.raw));
        f(Field{"unit.y", id}, bits(u.y.raw));
        f(Field{"unit.order", id}, static_cast<std::uint64_t>(u.order));
        f(Field{"unit.target_x", id}, bits(u.target_x));
        f(Field{"unit.target_y", id}, bits(u.target_y));
        f(Field{"unit.target_id", id}, bits(u.target_id));
        f(Field{"unit.cooldown", id}, bits(u.cooldown));
    }
    f(Field{"rng.state"}, w.rng.state());
    f(Field{"rng.increment"}, w.rng.increment());
    f(Field{"hit_percent"}, bits(w.hit_percent));
    f(Field{"held[0]"}, bits(w.held[0]));
    f(Field{"held[1]"}, bits(w.held[1]));
}

// "" when equal, otherwise "<field>: <a> vs <b>" for the first field that differs.
std::string first_difference(const World& a, const World& b);

// One "<field> <value>" line per field: diff two machines' dumps to see a desync.
std::string dump(const World& w);

}  // namespace lockstep
