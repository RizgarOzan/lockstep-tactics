#pragma once
// FNV-1a over fixed-width integers, fed byte by byte in little-endian order.
// Only fixed-width types go in: size_t is 64-bit on MSVC x64 and 32-bit on
// wasm32, so hashing one would make the two builds disagree for no real reason.
#include <cstdint>
#include <string_view>

namespace lockstep {

class StateHash {
public:
    void add(std::uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            byte(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    void add(std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            byte(static_cast<std::uint8_t>(v >> (8 * i)));
        }
    }
    void add(std::int32_t v) { add(static_cast<std::uint32_t>(v)); }
    void add(std::string_view s) {
        for (char c : s) {
            byte(static_cast<std::uint8_t>(c));
        }
    }
    std::uint64_t value() const { return h_; }

private:
    void byte(std::uint8_t b) {
        h_ ^= b;
        h_ *= 0x100000001b3ULL;
    }
    std::uint64_t h_ = 0xcbf29ce484222325ULL;
};

}  // namespace lockstep
