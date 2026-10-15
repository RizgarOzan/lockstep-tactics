#include "lockstep/rollback.hpp"

#include <charconv>
#include <cinttypes>
#include <cstdio>

namespace lockstep {

namespace {

// The frame is Lockstep's: "f <player> <tick> <count> <last confirmed tick> <hash>"
// followed by the command lines. Here <tick> is the tick the orders ran on.
struct Reader {
    std::string_view rest;
    std::string_view token() {
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        std::size_t n = 0;
        while (n < rest.size() && rest[n] != ' ' && rest[n] != '\n') ++n;
        const std::string_view tok = rest.substr(0, n);
        rest.remove_prefix(n);
        return tok;
    }
    bool end_line() {
        while (!rest.empty() && rest.front() == ' ') rest.remove_prefix(1);
        if (rest.empty() || rest.front() != '\n') return false;
        rest.remove_prefix(1);
        return true;
    }
    template <class T>
    bool number(T& out, int base = 10) {
        const std::string_view tok = token();
        const auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), out, base);
        return !tok.empty() && ec == std::errc{} && ptr == tok.data() + tok.size();
    }
};

std::vector<Command> merged(const std::vector<Command>& a, const std::vector<Command>* b) {
    std::vector<Command> out = a;
    if (b) out.insert(out.end(), b->begin(), b->end());
    return out;
}

}  // namespace

Rollback::Rollback(std::uint32_t local, std::uint64_t seed, std::uint32_t max_lag)
    : local_(local), max_lag_(max_lag < 1 ? 1 : max_lag), base_(make_world(seed)), present_(base_) {
    replay_.seed = seed;
}

std::string Rollback::advance(const std::vector<Command>& orders) {
    if (!may_advance()) return {};
    const std::uint32_t tick = present_.tick + 1;
    auto& mine = local_orders_[tick];
    for (const Command& c : orders) {
        if (c.player == local_) mine.push_back(c);
    }
    const auto remote = remote_orders_.find(tick);
    step(present_, merged(mine, remote == remote_orders_.end() ? nullptr : &remote->second));
    std::uint64_t last_hash = 0;
    if (confirmed_ > 0) last_hash = replay_.hashes[confirmed_ - 1];
    char head[96];
    std::snprintf(head, sizeof head, "f %u %u %zu %u %016" PRIx64 "\n", local_, tick, mine.size(), confirmed_, last_hash);
    std::string out = head;
    for (const Command& c : mine) {
        out += "c " + std::to_string(c.player) + " " + std::to_string(c.unit_id) + " " +
               std::to_string(static_cast<int>(c.order)) + " " + std::to_string(c.target_x) + " " +
               std::to_string(c.target_y) + " " + std::to_string(c.target_id) + "\n";
    }
    // The peer may already have sent this tick: then it is confirmed now.
    confirm_and_resimulate();
    return out;
}

bool Rollback::receive(std::string_view frame) {
    Reader in{frame};
    std::uint32_t player = 0, tick = 0, last = 0;
    std::size_t count = 0;
    std::uint64_t hash = 0;
    if (in.token() != "f" || !in.number(player) || !in.number(tick) || !in.number(count) || !in.number(last) ||
        !in.number(hash, 16) || !in.end_line() || player == local_ || player > 1) {
        return false;
    }
    std::vector<Command> theirs;
    for (std::size_t i = 0; i < count; ++i) {
        Command c;
        int order = 0;
        if (in.token() != "c" || !in.number(c.player) || !in.number(c.unit_id) || !in.number(order) ||
            !in.number(c.target_x) || !in.number(c.target_y) || !in.number(c.target_id) || !in.end_line() ||
            order < 0 || order > 2 || c.player != player) {
            return false;
        }
        c.order = static_cast<Order>(order);
        theirs.push_back(c);
    }
    if (!in.rest.empty()) return false;
    if (tick <= confirmed_) return false;  // older than anything kept; cannot happen with an ordered channel
    remote_orders_[tick] = std::move(theirs);
    if (last > 0) remote_hashes_[last] = hash;
    confirm_and_resimulate();
    return true;
}

void Rollback::confirm_and_resimulate() {
    // 1. Confirm every tick from confirmed_+1 on that has both sides' orders,
    //    advancing base_ and recording the replay and the hash.
    bool moved = false;
    while (true) {
        const std::uint32_t next = confirmed_ + 1;
        const auto mine = local_orders_.find(next);
        const auto theirs = remote_orders_.find(next);
        if (mine == local_orders_.end() || theirs == remote_orders_.end()) break;
        std::vector<Command> both = merged(mine->second, &theirs->second);
        step(base_, both);
        replay_.commands.push_back(std::move(both));
        replay_.hashes.push_back(hash(base_));
        const auto claim = remote_hashes_.find(next);
        if (!desync_ && claim != remote_hashes_.end() && claim->second != replay_.hashes.back()) desync_ = next;
        local_orders_.erase(mine);
        remote_orders_.erase(theirs);
        remote_hashes_.erase(next);
        confirmed_ = next;
        moved = true;
    }
    // A claim that arrived before we confirmed that tick is checked above;
    // one for a tick we had already confirmed is checked here.
    for (auto it = remote_hashes_.begin(); it != remote_hashes_.end();) {
        if (it->first <= confirmed_ && it->first > 0 && it->first <= replay_.hashes.size()) {
            if (!desync_ && it->second != replay_.hashes[it->first - 1]) desync_ = it->first;
            it = remote_hashes_.erase(it);
        } else {
            ++it;
        }
    }
    // 2. Re-simulate the present from the new base with every order known.
    //    Only needed when something changed: a confirmation moved the base,
    //    or a peer frame landed on a tick we had already run.
    if (!moved && remote_orders_.empty()) return;
    const std::uint32_t target = present_.tick;
    if (base_.tick == target) {
        present_ = base_;
        return;
    }
    World w = base_;
    for (std::uint32_t t = w.tick + 1; t <= target; ++t) {
        const auto mine = local_orders_.find(t);
        const auto theirs = remote_orders_.find(t);
        static const std::vector<Command> none;
        step(w, merged(mine == local_orders_.end() ? none : mine->second,
                       theirs == remote_orders_.end() ? nullptr : &theirs->second));
        ++resimulated_;
    }
    present_ = w;
}

}  // namespace lockstep
