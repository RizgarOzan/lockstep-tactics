#include <doctest/doctest.h>

#include <array>

#include "lockstep/world.hpp"

TEST_CASE("the same inputs give the same hash") {
    const std::array<lockstep::Input, 2> inputs{{{0, 3}, {1, 9}}};
    lockstep::World a, b;
    for (int i = 0; i < 100; ++i) {
        lockstep::step(a, inputs);
        lockstep::step(b, inputs);
    }
    CHECK(a.tick == 100);
    CHECK(lockstep::hash(a) == lockstep::hash(b));
}

TEST_CASE("a different input changes the hash") {
    lockstep::World a, b;
    const std::array<lockstep::Input, 1> one{{{0, 1}}};
    const std::array<lockstep::Input, 1> two{{{0, 2}}};
    lockstep::step(a, one);
    lockstep::step(b, two);
    CHECK(lockstep::hash(a) != lockstep::hash(b));
}
