#include <doctest/doctest.h>

#include "lockstep/hash.hpp"

TEST_CASE("FNV-1a matches the published test vectors") {
    lockstep::StateHash empty;
    CHECK(empty.value() == 0xcbf29ce484222325ULL);

    lockstep::StateHash a;
    a.add("a");
    CHECK(a.value() == 0xaf63dc4c8601ec8cULL);
}

TEST_CASE("integers are hashed little-endian, independent of the host") {
    lockstep::StateHash as_int;
    as_int.add(std::uint32_t{0x64636261});  // "abcd" in little-endian bytes
    lockstep::StateHash as_text;
    as_text.add("abcd");
    CHECK(as_int.value() == as_text.value());
}

TEST_CASE("signed values hash as their two's complement bits") {
    lockstep::StateHash s;
    s.add(std::int32_t{-1});
    lockstep::StateHash u;
    u.add(std::uint32_t{0xffffffff});
    CHECK(s.value() == u.value());
}
