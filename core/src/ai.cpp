#include "lockstep/ai.hpp"

#include <optional>
#include <utility>

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

// The neighbouring floor tile farthest from `from`, if any is farther than
// where `u` stands. Eight neighbours in a fixed order; the first best wins.
std::optional<std::pair<int, int>> step_away(const Map& map, const Unit& u, const Unit& from) {
    const int ux = tile_of(u.x), uy = tile_of(u.y), fx = tile_of(from.x), fy = tile_of(from.y);
    std::optional<std::pair<int, int>> best;
    std::int64_t best_d = static_cast<std::int64_t>(ux - fx) * (ux - fx) + static_cast<std::int64_t>(uy - fy) * (uy - fy);
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int x = ux + dx, y = uy + dy;
            if (x <= 0 || y <= 0 || x >= Map::kWidth - 1 || y >= Map::kHeight - 1) continue;
            if (map.at(x, y) != Tile::Floor) continue;
            // A diagonal step needs both orthogonal neighbours open, or the
            // walk would slide along a wall instead of moving away.
            if (dx != 0 && dy != 0 && (map.at(ux + dx, uy) != Tile::Floor || map.at(ux, uy + dy) != Tile::Floor)) continue;
            const std::int64_t d = static_cast<std::int64_t>(x - fx) * (x - fx) + static_cast<std::int64_t>(y - fy) * (y - fy);
            if (d > best_d) {
                best_d = d;
                best = std::make_pair(x, y);
            }
        }
    }
    return best;
}

bool can_hit(const World& w, const Unit& u, const Unit& e) {
    const std::int64_t r = stats(u.type).range.raw >> Fixed::kFracBits;
    return dist2(u, e) <= r * r && line_of_sight(w.map, tile_of(u.x), tile_of(u.y), tile_of(e.x), tile_of(e.y));
}

bool holding(const Unit& u) {
    return u.order == Order::Move && u.target_x == tile_of(u.x) && u.target_y == tile_of(u.y);
}

// The tile a unit will step to for its current order, with everyone else
// as walls, or nothing when it is not walking anywhere.
std::optional<std::pair<int, int>> heading(const World& w, const Unit& u) {
    int gx, gy;
    if (u.order == Order::Move) {
        gx = u.target_x;
        gy = u.target_y;
    } else if (u.order == Order::Attack) {
        const Unit* t = nullptr;
        for (const Unit& o : w.units) {
            if (o.id == u.target_id) t = &o;
        }
        if (t == nullptr || can_hit(w, u, *t)) return std::nullopt;
        gx = tile_of(t->x);
        gy = tile_of(t->y);
    } else {
        return std::nullopt;
    }
    Map blocked = w.map;
    for (const Unit& o : w.units) {
        const int ox = tile_of(o.x), oy = tile_of(o.y);
        if (o.id != u.id && (ox != gx || oy != gy)) blocked.set(ox, oy, Tile::Wall);
    }
    if (line_clear(blocked, tile_of(u.x), tile_of(u.y), gx, gy)) {
        // The straight walk's first tile is whichever neighbour the segment
        // enters; the step along it is close enough for a head-on check.
        const int dx = gx - tile_of(u.x), dy = gy - tile_of(u.y);
        const int ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
        if (ax == 0 && ay == 0) return std::nullopt;
        return ax >= ay ? std::make_pair(tile_of(u.x) + (dx > 0 ? 1 : -1), tile_of(u.y))
                        : std::make_pair(tile_of(u.x), tile_of(u.y) + (dy > 0 ? 1 : -1));
    }
    if (const auto step = next_step(blocked, tile_of(u.x), tile_of(u.y), gx, gy)) return step;
    // Boxed in by teammates: the direction it would take through them is
    // what the head-on check needs, so ask again with walls only.
    return next_step(w.map, tile_of(u.x), tile_of(u.y), gx, gy);
}

// A teammate about to block `u` for the rest of the match: one standing on
// the tile `u` wants next and heading for `u`'s own tile, or one heading
// for that same tile from the other side (then only the higher id yields,
// so exactly one of the two steps aside).
const Unit* head_on(const World& w, std::uint32_t player, const Unit& u, const std::pair<int, int>& next) {
    const int ux = tile_of(u.x), uy = tile_of(u.y);
    for (const Unit& m : w.units) {
        if (m.owner != player || m.id == u.id) continue;
        const auto theirs = heading(w, m);
        if (!theirs) continue;
        const int mx = tile_of(m.x), my = tile_of(m.y);
        if (mx == next.first && my == next.second && theirs->first == ux && theirs->second == uy) return &m;
        if (theirs->first == next.first && theirs->second == next.second && m.id < u.id) {
            const int facing = (next.first - ux) * (next.first - mx) + (next.second - uy) * (next.second - my);
            if (facing < 0) return &m;
        }
    }
    return nullptr;
}

// A free neighbouring floor tile off the line between `u` and `other`,
// the one nearest `u`'s goal first; nothing when boxed in.
std::optional<std::pair<int, int>> side_tile(const World& w, const Unit& u, const Unit& other, int gx, int gy) {
    const int ux = tile_of(u.x), uy = tile_of(u.y);
    const int ox = tile_of(other.x), oy = tile_of(other.y);
    std::optional<std::pair<int, int>> best;
    std::int64_t best_d = 0;
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dy == 0) continue;
            const int x = ux + dx, y = uy + dy;
            if (x == ox && y == oy) continue;
            if (x <= 0 || y <= 0 || x >= Map::kWidth - 1 || y >= Map::kHeight - 1) continue;
            if (w.map.at(x, y) != Tile::Floor) continue;
            if (dx != 0 && dy != 0 && (w.map.at(ux + dx, uy) != Tile::Floor || w.map.at(ux, uy + dy) != Tile::Floor)) continue;
            bool taken = false;
            for (const Unit& o : w.units) taken |= tile_of(o.x) == x && tile_of(o.y) == y;
            if (taken) continue;
            // Off the line: not the tile the other unit is heading into either.
            const std::int64_t d = static_cast<std::int64_t>(x - gx) * (x - gx) + static_cast<std::int64_t>(y - gy) * (y - gy);
            if (!best || d < best_d) {
                best = std::make_pair(x, y);
                best_d = d;
            }
        }
    }
    return best;
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

        // An Archer keeps its distance: with a melee unit inside two tiles it
        // shoots if the shot is ready, otherwise it spends the cooldown
        // stepping to whichever neighbouring tile is farthest from that unit.
        // Its range is four, so the step back rarely costs it the next shot;
        // it still loses to a Soldier, which is faster, and that is the matchup.
        if (u.type == UnitType::Archer && u.cooldown > 0 && near <= 4 && nearest->type != UnitType::Archer) {
            if (const auto away = step_away(w.map, u, *nearest)) {
                if (u.order != Order::Move || u.target_x != away->first || u.target_y != away->second) {
                    out.push_back(Command::move(player, u.id, away->first, away->second));
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
            // Two teammates meeting head-on in a one-wide gap would stand there
            // until the match ended: the one that sees it steps to a free
            // tile beside the line, and the other walks through. Checked
            // before the hold, because a holding unit is exactly the kind of
            // wall this is about.
            if (const auto next = heading(w, u)) {
                if (const Unit* m = head_on(w, player, u, *next)) {
                    if (const auto side = side_tile(w, u, *m, tile_of(focus->x), tile_of(focus->y))) {
                        if (u.order != Order::Move || u.target_x != side->first || u.target_y != side->second) {
                            out.push_back(Command::move(player, u.id, side->first, side->second));
                        }
                        continue;
                    }
                }
            }
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
