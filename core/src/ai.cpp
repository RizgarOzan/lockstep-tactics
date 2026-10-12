#include "lockstep/ai.hpp"

namespace lockstep {

namespace {

int tile_of(Fixed v) { return v.raw >> Fixed::kFracBits; }

// Squared distance in whole tiles. raw is Q16.16, so raw*raw is Q32.32 and
// the integer part sits above bit 32; int64 holds it with room to spare.
std::int64_t dist2(const Unit& a, const Unit& b) {
    const std::int64_t dx = static_cast<std::int64_t>(a.x.raw) - b.x.raw;
    const std::int64_t dy = static_cast<std::int64_t>(a.y.raw) - b.y.raw;
    return (dx * dx + dy * dy) >> 32;
}

// +1 when `a` has the bonus against `b`, -1 when `b` has it against `a`.
int matchup(UnitType a, UnitType b) {
    if (damage(a, b) > stats(a).damage) return 1;
    if (damage(b, a) > stats(b).damage) return -1;
    return 0;
}

int sign(std::int32_t v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }

}  // namespace

std::vector<Command> ai_orders(const World& w, std::uint32_t player) {
    std::vector<Command> out;
    for (const Unit& u : w.units) {
        if (u.owner != player) continue;

        // The closest enemy wins, but a good matchup is worth twelve tiles²
        // and every hit point the enemy is missing one more. Ties go to the
        // lower id, which is just the order units are stored in.
        const Unit* target = nullptr;
        const Unit* nearest = nullptr;
        std::int64_t best = 0, near = 0;
        for (const Unit& e : w.units) {
            if (e.owner == player) continue;
            const std::int64_t d = dist2(u, e);
            const std::int64_t score = d - 12 * matchup(u.type, e.type) + e.hp;
            if (target == nullptr || score < best) {
                target = &e;
                best = score;
            }
            if (nearest == nullptr || d < near) {
                nearest = &e;
                near = d;
            }
        }
        if (target == nullptr) continue;

        // An Archer with a melee unit on top of it and no shot ready steps
        // back one tile; it shoots again the moment the cooldown ends.
        if (u.type == UnitType::Archer && u.cooldown > 0 && near <= 1 && nearest->type != UnitType::Archer) {
            const int tx = tile_of(u.x) + sign(u.x.raw - nearest->x.raw);
            const int ty = tile_of(u.y) + sign(u.y.raw - nearest->y.raw);
            const bool inside = tx > 0 && ty > 0 && tx < Map::kWidth - 1 && ty < Map::kHeight - 1;
            if (inside && w.map.at(tx, ty) == Tile::Floor && (tx != tile_of(u.x) || ty != tile_of(u.y))) {
                if (u.order != Order::Move || u.target_x != tx || u.target_y != ty) {
                    out.push_back(Command::move(player, u.id, tx, ty));
                }
                continue;
            }
        }

        if (u.order != Order::Attack || u.target_id != target->id) {
            out.push_back(Command::attack(player, u.id, target->id));
        }
    }
    return out;
}

}  // namespace lockstep
