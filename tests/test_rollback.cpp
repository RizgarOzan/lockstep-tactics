#include <doctest/doctest.h>

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "lockstep/random_orders.hpp"
#include "lockstep/rollback.hpp"
#include "lockstep/world.hpp"

using namespace lockstep;

namespace {

// One side with a channel that delivers each frame a few ticks late.
struct Side {
    Rollback rb;
    std::uint64_t rng;
    std::deque<std::pair<int, std::string>> inbox;  // deliver-at tick, frame
    Side(std::uint32_t player, std::uint64_t seed) : rb(player, seed), rng(seed * 11 + player) {}

    std::vector<Command> orders() {
        std::vector<Command> mine = random_orders(rb.world(), rng);
        std::erase_if(mine, [&](const Command& c) { return c.player != rb.local(); });
        return mine;
    }
};

// The same match with no network at all: both sides' orders in one step()
// per tick. Rollback must end up confirming exactly these hashes.
std::vector<std::uint64_t> reference(std::uint64_t seed, const std::vector<std::vector<Command>>& a,
                                     const std::vector<std::vector<Command>>& b) {
    World w = make_world(seed);
    std::vector<std::uint64_t> out;
    for (std::size_t t = 0; t < a.size(); ++t) {
        std::vector<Command> both = a[t];
        both.insert(both.end(), b[t].begin(), b[t].end());
        step(w, both);
        out.push_back(hash(w));
    }
    return out;
}

}  // namespace

TEST_CASE("rollback confirms the same hashes as a match with no latency, whatever the delay") {
    for (int lag : {0, 1, 3, 7}) {
        Side a(0, 21), b(1, 21);
        std::vector<std::vector<Command>> sent_a, sent_b;
        std::uint64_t jitter = 5;
        for (int t = 1; t <= 200; ++t) {
            // Deliver what is due.
            for (Side* s : {&a, &b}) {
                while (!s->inbox.empty() && s->inbox.front().first <= t) {
                    REQUIRE(s->rb.receive(s->inbox.front().second));
                    s->inbox.pop_front();
                }
            }
            const auto oa = a.orders(), ob = b.orders();
            const std::string fa = a.rb.advance(oa), fb = b.rb.advance(ob);
            REQUIRE_FALSE(fa.empty());
            REQUIRE_FALSE(fb.empty());
            sent_a.push_back(oa);
            sent_b.push_back(ob);
            const int da = lag == 0 ? 0 : static_cast<int>(splitmix64(jitter) % static_cast<std::uint64_t>(lag + 1));
            const int db = lag == 0 ? 0 : static_cast<int>(splitmix64(jitter) % static_cast<std::uint64_t>(lag + 1));
            b.inbox.push_back({t + da, fa});
            a.inbox.push_back({t + db, fb});
        }
        // Flush the channels.
        for (Side* s : {&a, &b}) {
            while (!s->inbox.empty()) {
                REQUIRE(s->rb.receive(s->inbox.front().second));
                s->inbox.pop_front();
            }
        }
        CHECK(a.rb.confirmed() == 200);
        CHECK(b.rb.confirmed() == 200);
        const auto ref = reference(21, sent_a, sent_b);
        CHECK(a.rb.replay().hashes == ref);
        CHECK(b.rb.replay().hashes == ref);
        CHECK(hash(a.rb.world()) == ref.back());  // the present equals the confirmed end once all is in
        CHECK_FALSE(a.rb.desync().has_value());
        CHECK_FALSE(b.rb.desync().has_value());
        // A frame one tick late arrives before the next advance, so the base
        // simply catches up; only a longer lag makes the present re-run.
        if (lag == 0) CHECK(a.rb.resimulated() == 0);
        if (lag >= 3) CHECK(a.rb.resimulated() > 0);
    }
}

TEST_CASE("the present runs ahead of the confirmed tick and stops at the lag limit") {
    Rollback rb(0, 4, 5);
    for (int t = 0; t < 5; ++t) CHECK_FALSE(rb.advance({}).empty());
    CHECK(rb.tick() == 5);
    CHECK(rb.confirmed() == 0);
    CHECK(rb.advance({}).empty());  // five ticks ahead with nothing confirmed: wait
    Rollback peer(1, 4, 5);
    REQUIRE(rb.receive(peer.advance({})));  // peer's tick 1
    CHECK(rb.confirmed() == 1);
    CHECK_FALSE(rb.advance({}).empty());
}

TEST_CASE("a late order changes the present and the confirmed replay keeps it") {
    Rollback a(0, 8), b(1, 8);
    // Blue runs three ticks with no orders; Orange gave an order on tick 1
    // that only arrives afterwards.
    const std::string f1 = b.advance({Command::move(1, 4, 7, 7)});
    for (int i = 0; i < 3; ++i) a.advance({});
    const std::uint64_t before = hash(a.world());
    REQUIRE(a.receive(f1));
    CHECK(hash(a.world()) != before);  // the Orange unit has been walking for three ticks now
    CHECK(a.confirmed() == 1);
    REQUIRE(a.replay().commands.size() == 1);
    CHECK(a.replay().commands[0].size() == 1);
    CHECK(a.replay().commands[0][0].unit_id == 4);
    CHECK(a.resimulated() == 2);  // tick 1 was confirmed, ticks 2 and 3 re-run
}

TEST_CASE("a peer whose confirmed hash differs is reported at that tick") {
    Rollback a(0, 9), b(1, 9);
    for (int t = 1; t <= 4; ++t) {
        const std::string fa = a.advance({}), fb = b.advance({});
        REQUIRE(a.receive(fb));
        REQUIRE(b.receive(fa));
    }
    // Forge a frame from b claiming a different hash for tick 4.
    const std::string forged = "f 1 5 0 4 0123456789abcdef\n";
    REQUIRE(a.receive(forged));
    REQUIRE(a.desync().has_value());
    CHECK(*a.desync() == 4);
}
