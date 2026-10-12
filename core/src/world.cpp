#include "lockstep/world.hpp"

#include "lockstep/hash.hpp"

namespace lockstep {

void step(World& world, std::span<const Input> inputs) {
    // Placeholder rule. Unsigned arithmetic on purpose: signed overflow is
    // undefined behaviour, and an optimiser may treat it differently per
    // compiler, which is exactly a desync.
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
    h.add(world.placeholder);
    return h.value();
}

}  // namespace lockstep
