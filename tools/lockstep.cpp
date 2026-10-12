// Replay files and desync hunting.
//
//   lockstep record <seed> <ticks> <file>   play random orders, save commands + per-tick hashes
//   lockstep verify <file>                  replay here; "ok" or the first tick whose hash differs
//   lockstep dump <file> <tick>             every field of the state after <tick>, one per line
//
// Two machines that disagree: run verify on the second one to get the tick,
// then dump that tick on both and diff the two dumps.
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "lockstep/fields.hpp"
#include "lockstep/random_orders.hpp"
#include "lockstep/replay.hpp"

namespace {

int usage() {
    std::fputs("usage: lockstep record <seed> <ticks> <file> | verify <file> | dump <file> <tick>\n", stderr);
    return 2;
}

bool load(const char* path, lockstep::Replay& out) {
    std::ifstream in(path, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto r = lockstep::from_text(text);
    if (!r) {
        std::fprintf(stderr, "%s: not a readable replay\n", path);
        return false;
    }
    out = std::move(*r);
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) return usage();
    const std::string cmd = argv[1];

    if (cmd == "record" && argc == 5) {
        const std::uint64_t seed = std::strtoull(argv[2], nullptr, 10);
        const auto ticks = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10));
        std::uint64_t rng = seed;
        const auto r = lockstep::record(seed, ticks, [&](const lockstep::World& w) {
            return lockstep::random_orders(w, rng);
        });
        std::ofstream(argv[4], std::ios::binary) << lockstep::to_text(r);
        std::printf("recorded %u ticks\n", ticks);
        return 0;
    }

    lockstep::Replay r;
    if (cmd == "verify" && argc == 3) {
        if (!load(argv[2], r)) return 2;
        if (const auto tick = lockstep::first_divergence(r.hashes, lockstep::play(r))) {
            std::printf("desync at tick %u\n", *tick);
            return 1;
        }
        std::printf("ok: %zu ticks match\n", r.hashes.size());
        return 0;
    }
    if (cmd == "dump" && argc == 4) {
        if (!load(argv[2], r)) return 2;
        const auto tick = static_cast<std::uint32_t>(std::strtoul(argv[3], nullptr, 10));
        std::fputs(lockstep::dump(lockstep::state_at(r, tick)).c_str(), stdout);
        return 0;
    }
    return usage();
}
