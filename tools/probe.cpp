// Plays the AI (Orange) against five scripted opponents (Blue) for N seeded
// matches each and prints a table: the five from docs/notes/11, so an AI
// change can be measured against more than random orders.
//
//   probe [seeds] [ticks]     defaults: 1000 seeds, 600 ticks
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "lockstep/ai.hpp"
#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"

using namespace lockstep;

namespace {

int tile_of(Fixed v) { return v.raw >> Fixed::kFracBits; }

std::int64_t dist2(const Unit& a, const Unit& b) {
    const std::int64_t dx = static_cast<std::int64_t>(a.x.raw) - b.x.raw;
    const std::int64_t dy = static_cast<std::int64_t>(a.y.raw) - b.y.raw;
    return (dx * dx + dy * dy) >> 32;
}

void attack_if_new(std::vector<Command>& out, const Unit& u, std::uint32_t target) {
    if (u.order != Order::Attack || u.target_id != target) out.push_back(Command::attack(0, u.id, target));
}

const Unit* nearest_enemy(const World& w, const Unit& u) {
    const Unit* best = nullptr;
    for (const Unit& e : w.units) {
        if (e.owner == 0) continue;
        if (best == nullptr || dist2(u, e) < dist2(u, *best)) best = &e;
    }
    return best;
}

// Everyone attacks the enemy Archer while it lives, then the most wounded.
std::vector<Command> focus_fire(const World& w) {
    std::vector<Command> out;
    const Unit* target = nullptr;
    for (const Unit& e : w.units) {
        if (e.owner == 0) continue;
        if (target == nullptr || (e.type == UnitType::Archer && target->type != UnitType::Archer) ||
            (e.type == target->type && e.hp < target->hp)) {
            target = &e;
        }
    }
    if (target == nullptr) return out;
    for (const Unit& u : w.units) {
        if (u.owner == 0) attack_if_new(out, u, target->id);
    }
    return out;
}

// Stands where it spawned until an enemy comes within three tiles, then
// attacks the nearest.
std::vector<Command> hold_spawn(const World& w) {
    std::vector<Command> out;
    for (const Unit& u : w.units) {
        if (u.owner != 0) continue;
        const Unit* e = nearest_enemy(w, u);
        if (e != nullptr && dist2(u, *e) <= 9) attack_if_new(out, u, e->id);
    }
    return out;
}

// Melee attacks the nearest; the Archer steps to the neighbouring floor tile
// farthest from any enemy within three tiles, and shoots otherwise.
std::vector<Command> archer_kites(const World& w) {
    std::vector<Command> out;
    for (const Unit& u : w.units) {
        if (u.owner != 0) continue;
        const Unit* e = nearest_enemy(w, u);
        if (e == nullptr) continue;
        if (u.type == UnitType::Archer && dist2(u, *e) <= 9) {
            const int ux = tile_of(u.x), uy = tile_of(u.y), ex = tile_of(e->x), ey = tile_of(e->y);
            int bx = ux, by = uy;
            std::int64_t best = static_cast<std::int64_t>(ux - ex) * (ux - ex) + static_cast<std::int64_t>(uy - ey) * (uy - ey);
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int x = ux + dx, y = uy + dy;
                    if (x <= 0 || y <= 0 || x >= Map::kWidth - 1 || y >= Map::kHeight - 1) continue;
                    if (w.map.at(x, y) != Tile::Floor) continue;
                    const std::int64_t d = static_cast<std::int64_t>(x - ex) * (x - ex) + static_cast<std::int64_t>(y - ey) * (y - ey);
                    if (d > best) {
                        best = d;
                        bx = x;
                        by = y;
                    }
                }
            }
            if ((bx != ux || by != uy) && (u.order != Order::Move || u.target_x != bx || u.target_y != by)) {
                out.push_back(Command::move(0, u.id, bx, by));
            } else if (bx == ux && by == uy) {
                attack_if_new(out, u, e->id);
            }
        } else {
            attack_if_new(out, u, e->id);
        }
    }
    return out;
}

struct Opponent {
    const char* name;
    std::vector<Command> (*orders)(const World&, std::uint64_t&);
};

std::vector<Command> blue_random(const World& w, std::uint64_t& rng) {
    std::vector<Command> out = random_orders(w, rng);
    std::erase_if(out, [](const Command& c) { return c.player == 1; });
    return out;
}
std::vector<Command> blue_itself(const World& w, std::uint64_t&) { return ai_orders(w, 0); }
std::vector<Command> blue_focus(const World& w, std::uint64_t&) { return focus_fire(w); }
std::vector<Command> blue_hold(const World& w, std::uint64_t&) { return hold_spawn(w); }
std::vector<Command> blue_kite(const World& w, std::uint64_t&) { return archer_kites(w); }

}  // namespace

int main(int argc, char** argv) {
    const std::uint32_t seeds = argc > 1 ? static_cast<std::uint32_t>(std::strtoul(argv[1], nullptr, 10)) : 1000;
    const std::uint32_t ticks = argc > 2 ? static_cast<std::uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 600;
    const Opponent opponents[] = {{"random", blue_random}, {"itself", blue_itself}, {"focus fire", blue_focus},
                                  {"hold spawn", blue_hold}, {"archer kites", blue_kite}};
    std::printf("| Opponent | AI wins | opponent wins | timeouts (%u ticks) | won on the objective (AI / opponent) |\n", ticks);
    std::printf("|---|---|---|---|---|\n");
    for (const Opponent& o : opponents) {
        int ai = 0, opp = 0, timeout = 0, ai_hold = 0, opp_hold = 0;
        for (std::uint32_t seed = 0; seed < seeds; ++seed) {
            std::uint64_t rng = seed;
            World w = make_world(seed);
            std::uint32_t t = 0;
            for (; t < ticks && winner(w) == -1; ++t) {
                std::vector<Command> cmds = o.orders(w, rng);
                const std::vector<Command> orange = ai_orders(w, 1);
                cmds.insert(cmds.end(), orange.begin(), orange.end());
                step(w, cmds);
            }
            const int win = winner(w);
            if (win == -1) ++timeout;
            else if (win == 1) ++ai, ai_hold += w.held[1] >= kHoldToWin;
            else if (win == 0) ++opp, opp_hold += w.held[0] >= kHoldToWin;
        }
        std::printf("| %s | %d | %d | %d | %d / %d |\n", o.name, ai, opp, timeout, ai_hold, opp_hold);
    }
    return 0;
}
