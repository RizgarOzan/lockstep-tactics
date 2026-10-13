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

bool can_hit(const World& w, const Unit& u, const Unit& e) {
    const std::int64_t r = stats(u.type).range.raw >> Fixed::kFracBits;
    return dist2(u, e) <= r * r && line_of_sight(w.map, tile_of(u.x), tile_of(u.y), tile_of(e.x), tile_of(e.y));
}

bool holding(const Unit& u) {
    return u.order == Order::Move && u.target_x == tile_of(u.x) && u.target_y == tile_of(u.y);
}

// How far the team is spread, measured towards `target`: the squared
// distance of the farthest teammate minus this unit's own.
std::int64_t gap_behind(const World& w, std::uint32_t player, const Unit& u, const Unit& target) {
    std::int64_t farthest = 0;
    for (const Unit& m : w.units) {
        if (m.owner != player) continue;
        const std::int64_t d = dist2(m, target);
        if (d > farthest) farthest = d;
    }
    return farthest - dist2(u, target);
}

}  // namespace

std::vector<Command> ai_orders(const World& w, std::uint32_t player) {
    std::vector<Command> out;
    std::vector<const Unit*> mine, theirs;
    for (const Unit& u : w.units) (u.owner == player ? mine : theirs).push_back(&u);
    if (mine.empty() || theirs.empty()) return out;

    // The team target: the enemy the whole side converges on, so damage lands
    // on one unit at a time instead of being spread over three. Scored for
    // the team: total squared distance, minus twelve per favourable matchup,
    // plus the enemy's remaining health per unit of ours. Ties go to the
    // lower id, which is just the order units are stored in.
    const Unit* focus = nullptr;
    std::int64_t best = 0;
    for (const Unit* e : theirs) {
        std::int64_t score = 0;
        for (const Unit* u : mine) score += dist2(*u, *e) - 12 * matchup(u->type, e->type);
        score += static_cast<std::int64_t>(e->hp) * static_cast<std::int64_t>(mine.size());
        if (focus == nullptr || score < best) {
            focus = e;
            best = score;
        }
    }

    for (const Unit* up : mine) {
        const Unit& u = *up;
        const Unit* nearest = nullptr;
        std::int64_t near = 0;
        for (const Unit* e : theirs) {
            const std::int64_t d = dist2(u, *e);
            if (nearest == nullptr || d < near) {
                nearest = e;
                near = d;
            }
        }

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

        // Something already in reach is fought now, the best matchup and the
        // most wounded first; walking away from it to join the focus would
        // hand it free hits.
        const Unit* target = nullptr;
        std::int64_t local = 0;
        for (const Unit* e : theirs) {
            if (!can_hit(w, u, *e)) continue;
            const std::int64_t score = e->hp - 12 * matchup(u.type, e->type);
            if (target == nullptr || score < local) {
                target = e;
                local = score;
            }
        }

        if (target == nullptr) {
            // Nothing in reach: move on the focus, but not alone. A unit more
            // than five tiles² ahead of the slowest teammate waits where it
            // stands until the gap is under three; the two thresholds keep
            // it from flapping between the two orders every tick.
            const std::int64_t gap = gap_behind(w, player, u, *focus);
            const bool wait = holding(u) ? gap > 9 : gap > 25;
            if (wait && mine.size() > 1) {
                if (!holding(u)) out.push_back(Command::move(player, u.id, tile_of(u.x), tile_of(u.y)));
                continue;
            }
            target = focus;
        }

        if (u.order != Order::Attack || u.target_id != target->id) {
            out.push_back(Command::attack(player, u.id, target->id));
        }
    }
    return out;
}

}  // namespace lockstep
