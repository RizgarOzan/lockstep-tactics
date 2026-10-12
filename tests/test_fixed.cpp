#include <doctest/doctest.h>

#include <cstdint>

#include "lockstep/fixed.hpp"

using lockstep::Fixed;

TEST_CASE("integers round-trip and arithmetic is exact on representable values") {
    CHECK(Fixed::from_int(7).raw == 7 * 65536);
    const Fixed two_and_half = Fixed::from_ratio(5, 2);
    CHECK(two_and_half * Fixed::from_int(4) == Fixed::from_int(10));
    CHECK(Fixed::from_int(3) + Fixed::from_int(4) == Fixed::from_int(7));
    CHECK(Fixed::from_int(3) - Fixed::from_int(4) == Fixed::from_int(-1));
    CHECK(Fixed::from_int(10) / Fixed::from_int(4) == two_and_half);
}

TEST_CASE("negative values multiply with the right sign") {
    CHECK(Fixed::from_ratio(-3, 2) * Fixed::from_int(2) == Fixed::from_int(-3));
    CHECK(Fixed::from_int(-2) * Fixed::from_int(-3) == Fixed::from_int(6));
}

TEST_CASE("multiplication rounds toward negative infinity, on every target") {
    // The smallest step times one half: exactly half a step, so the result
    // must fall to the step below. Positive -> 0, negative -> -1 step.
    const Fixed half = Fixed::from_ratio(1, 2);
    CHECK((Fixed::from_raw(1) * half).raw == 0);
    CHECK((Fixed::from_raw(-1) * half).raw == -1);
}

TEST_CASE("division rounds toward negative infinity too") {
    // 1/3 and -1/3 are not representable; both must floor, so -1/3 is one
    // step below -(1/3), not its mirror image.
    const std::int32_t third = (Fixed::from_int(1) / Fixed::from_int(3)).raw;
    CHECK(third == 21845);
    CHECK((Fixed::from_int(-1) / Fixed::from_int(3)).raw == -21846);
}

TEST_CASE("comparison follows the value") {
    CHECK(Fixed::from_ratio(1, 3) < Fixed::from_ratio(1, 2));
    CHECK(Fixed::from_int(-1) < Fixed::from_raw(0));
}
