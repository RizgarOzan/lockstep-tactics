#pragma once
// What a click on the board means for one player. Pure: reads the World and
// returns a selection and maybe a Command; it never touches the state. The
// viewer calls it, and because the result is an ordinary Command, a match a
// person played records and verifies exactly like a random one.
#include <cstdint>
#include <optional>

#include "lockstep/world.hpp"

namespace lockstep {

// The unit standing on tile (tx, ty), lowest id first if two share it.
inline const Unit* unit_at(const World& w, int tx, int ty) {
    for (const Unit& u : w.units) {
        if ((u.x.raw >> Fixed::kFracBits) == tx && (u.y.raw >> Fixed::kFracBits) == ty) return &u;
    }
    return nullptr;
}

struct Click {
    std::uint32_t selected = 0;  // the unit now selected, 0 for none
    std::optional<Command> command;
};

// `selected` is the player's current selection (0 for none). Clicking an own
// unit selects it; with a selection, an enemy means attack and a floor tile
// means move. Walls, off-board tiles and clicks with nothing selected do
// nothing. The selection survives an order, so one unit can be re-ordered
// without picking it again.
inline Click click(const World& w, std::uint32_t player, std::uint32_t selected, int tx, int ty) {
    Click out{selected, std::nullopt};
    if (tx < 0 || ty < 0 || tx >= Map::kWidth || ty >= Map::kHeight) return out;
    if (const Unit* u = unit_at(w, tx, ty)) {
        if (u->owner == player) {
            out.selected = u->id;
        } else if (selected != 0) {
            out.command = Command::attack(player, selected, u->id);
        }
        return out;
    }
    if (selected != 0 && w.map.at(tx, ty) == Tile::Floor) {
        out.command = Command::move(player, selected, tx, ty);
    }
    return out;
}

}  // namespace lockstep
