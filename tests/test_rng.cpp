#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "lockstep/rng.hpp"

using lockstep::Pcg32;

TEST_CASE("matches the reference pcg32 demo output (seed 42, stream 54)") {
    // pcg32-demo from the PCG reference C implementation, first six numbers.
    Pcg32 rng(42u, 54u);
    const std::array<std::uint32_t, 6> expected{
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
    for (std::uint32_t want : expected) {
        CHECK(rng.next() == want);
    }
}

TEST_CASE("bounded draws stay in range and are reproducible") {
    Pcg32 a(7u, 1u), b(7u, 1u);
    for (int i = 0; i < 1000; ++i) {
        const std::uint32_t x = a.below(6);
        CHECK(x < 6u);
        CHECK(x == b.below(6));
    }
}

TEST_CASE("a bound of one always gives zero") {
    Pcg32 rng(1u, 1u);
    CHECK(rng.below(1) == 0u);
}

TEST_CASE("different streams diverge") {
    Pcg32 a(42u, 54u), b(42u, 55u);
    CHECK(a.next() != b.next());
}
