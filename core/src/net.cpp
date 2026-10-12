#include "lockstep/net.hpp"

#include <charconv>
#include <cinttypes>
#include <cstdio>

namespace lockstep {

// Frame, one record per line, the command line being the replay format's:
//   f <player> <tick> <command count> <last tick run> <hash after it, hex>
//   c <player> <unit> <order> <target_x> <target_y> <target_id>   × count
// Before any tick has run the last two fields are 0 and 0.

Lockstep::Lockstep(std::uint32_t local, std::uint32_t delay) : local_(local), delay_(delay < 1 ? 1 : delay) {
    // Ticks inside the first delay window run with nobody's orders.
    for (std::uint32_t t = 0; t < delay_; ++t) {
        local_orders_[t];
        remote_orders_[t];
    }
}

std::string Lockstep::submit(const std::vector<Command>& orders) {
    const std::uint32_t tick = next_ + delay_;
    if (local_orders_.count(tick)) return {};
    auto& mine = local_orders_[tick];
    for (const Command& c : orders) {
        if (c.player == local_) mine.push_back(c);
    }
    std::uint32_t last = 0;
    std::uint64_t hash = 0;
    if (!local_hashes_.empty()) {
        last = local_hashes_.rbegin()->first;
        hash = local_hashes_.rbegin()->second;
    }
    char head[96];
    std::snprintf(head, sizeof head, "f %u %u %zu %u %016" PRIx64 "\n", local_, tick, mine.size(), last, hash);
    std::string out = head;
    for (const Command& c : mine) {
        out += "c " + std::to_string(c.player) + " " + std::to_string(c.unit_id) + " " +
               std::to_string(static_cast<int>(c.order)) + " " + std::to_string(c.target_x) + " " +
               std::to_string(c.target_y) + " " + std::to_string(c.target_id) + "\n";
    }
    return out;
}

void Lockstep::ran(std::uint32_t tick, std::uint64_t hash) { local_hashes_[tick] = hash; }

namespace {

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

}  // namespace

bool Lockstep::receive(std::string_view frame) {
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
    remote_orders_[tick] = std::move(theirs);
    // A hash for a tick we have run too: the first disagreement is the desync.
    if (!desync_ && last > 0) {
        const auto mine = local_hashes_.find(last);
        if (mine != local_hashes_.end() && mine->second != hash) desync_ = last;
    }
    return true;
}

bool Lockstep::ready() const { return local_orders_.count(next_) && remote_orders_.count(next_); }

std::optional<std::vector<Command>> Lockstep::next() {
    if (!ready()) return std::nullopt;
    std::vector<Command> out = local_orders_[next_];
    const auto& theirs = remote_orders_[next_];
    out.insert(out.end(), theirs.begin(), theirs.end());
    ++next_;
    return out;
}

}  // namespace lockstep
