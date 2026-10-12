#pragma once
// Q16.16 fixed-point: a 32-bit integer counting 1/65536 steps. The simulation
// uses this instead of float so that MSVC and WebAssembly cannot disagree:
// integer arithmetic has one answer, float arithmetic can depend on the
// compiler's choice of instructions, fused multiply-adds and rounding modes.
//
// Both * and / round toward negative infinity. C++ integer division truncates
// toward zero, which would make -1/3 the mirror of 1/3 while -1*0.5 floors;
// one rule for both keeps the sign of an error predictable.
//
// Overflow is not checked. Values that leave the int32 range wrap (the
// int64 -> int32 conversion is modular since C++20), the same way on every
// target, so it stays a bug in the simulation rather than a desync.
#include <compare>
#include <cstdint>

namespace lockstep {

struct Fixed {
    std::int32_t raw = 0;

    static constexpr int kFracBits = 16;
    static constexpr std::int32_t kOne = 1 << kFracBits;

    static constexpr Fixed from_raw(std::int32_t r) { return Fixed{r}; }
    static constexpr Fixed from_int(std::int32_t i) {
        return Fixed{static_cast<std::int32_t>(static_cast<std::int64_t>(i) * kOne)};
    }
    // num/den, floored. Exact when den divides num * 65536.
    static constexpr Fixed from_ratio(std::int32_t num, std::int32_t den) {
        return from_int(num) / from_int(den);
    }

    friend constexpr Fixed operator+(Fixed a, Fixed b) {
        return Fixed{static_cast<std::int32_t>(static_cast<std::uint32_t>(a.raw) +
                                               static_cast<std::uint32_t>(b.raw))};
    }
    friend constexpr Fixed operator-(Fixed a, Fixed b) {
        return Fixed{static_cast<std::int32_t>(static_cast<std::uint32_t>(a.raw) -
                                               static_cast<std::uint32_t>(b.raw))};
    }
    friend constexpr Fixed operator*(Fixed a, Fixed b) {
        // Right shift of a negative value is arithmetic (floor) since C++20.
        const std::int64_t wide = static_cast<std::int64_t>(a.raw) * b.raw;
        return Fixed{static_cast<std::int32_t>(wide >> kFracBits)};
    }
    friend constexpr Fixed operator/(Fixed a, Fixed b) {
        // Precondition: b.raw != 0.
        const std::int64_t num = static_cast<std::int64_t>(a.raw) * kOne;
        std::int64_t q = num / b.raw;
        if ((num % b.raw != 0) && ((num < 0) != (b.raw < 0))) {
            --q;  // truncation went up; step down to the floor
        }
        return Fixed{static_cast<std::int32_t>(q)};
    }

    friend constexpr bool operator==(Fixed, Fixed) = default;
    friend constexpr auto operator<=>(Fixed, Fixed) = default;
};

}  // namespace lockstep
