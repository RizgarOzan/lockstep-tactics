#pragma once
// PCG32 (O'Neill, pcg-random.org): 64-bit state, 32-bit output. Written out
// here rather than taken from <random>: std::mt19937 itself is specified, but
// std::uniform_int_distribution and friends are not, so MSVC's STL and libc++
// turn the same engine output into different numbers.
#include <cstdint>

namespace lockstep {

class Pcg32 {
public:
    Pcg32(std::uint64_t seed, std::uint64_t stream) : inc_((stream << 1u) | 1u) {
        next();
        state_ += seed;
        next();
    }

    std::uint32_t next() {
        const std::uint64_t old = state_;
        state_ = old * 6364136223846793005ULL + inc_;
        const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
        const auto rot = static_cast<std::uint32_t>(old >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }

    // Uniform in [0, bound), without modulo bias: values below
    // 2^32 mod bound are redrawn. Precondition: bound > 0.
    std::uint32_t below(std::uint32_t bound) {
        const std::uint32_t threshold = (0u - bound) % bound;
        for (;;) {
            const std::uint32_t r = next();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }

    std::uint64_t state() const { return state_; }
    std::uint64_t increment() const { return inc_; }

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_;
};

}  // namespace lockstep
