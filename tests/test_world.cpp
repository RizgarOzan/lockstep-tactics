#include <doctest/doctest.h>

#include <vector>

#include "lockstep/world.hpp"

using namespace lockstep;

TEST_CASE("the same commands give the same hash") {
    World a = make_world(1), b = make_world(1);
    const std::vector<Command> commands{Command::move(0, 1, 6, 5), Command::attack(1, 5, 2)};
    for (int i = 0; i < 100; ++i) {
        step(a, i == 0 ? commands : std::vector<Command>{});
        step(b, i == 0 ? commands : std::vector<Command>{});
    }
    CHECK(a.tick == 100);
    CHECK(hash(a) == hash(b));
}

TEST_CASE("a different command changes the hash") {
    World a = make_world(1), b = make_world(1);
    const std::vector<Command> one{Command::move(0, 1, 6, 5)};
    const std::vector<Command> two{Command::move(0, 1, 6, 6)};
    step(a, one);
    step(b, two);
    for (int i = 0; i < 10; ++i) {
        step(a, {});
        step(b, {});
    }
    CHECK(hash(a) != hash(b));
}
