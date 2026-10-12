#include "lockstep/replay.hpp"

#include <charconv>
#include <cinttypes>
#include <cstdio>
#include <utility>

#include "lockstep/fields.hpp"

namespace lockstep {

std::string describe(const Field& f) {
    const std::string name = f.name;
    if (name == "map") {
        return "map[" + std::to_string(f.a) + "," + std::to_string(f.b) + "]";
    }
    if (name.rfind("unit.", 0) == 0) {
        return "unit#" + std::to_string(f.a) + name.substr(4);
    }
    return name;
}

namespace {

std::vector<std::pair<Field, std::uint64_t>> flatten(const World& w) {
    std::vector<std::pair<Field, std::uint64_t>> out;
    visit_fields(w, [&](const Field& f, std::uint64_t v) { out.emplace_back(f, v); });
    return out;
}

bool same_field(const Field& x, const Field& y) {
    return std::string_view(x.name) == y.name && x.a == y.a && x.b == y.b;
}

}  // namespace

std::string first_difference(const World& a, const World& b) {
    const auto fa = flatten(a);
    const auto fb = flatten(b);
    const std::size_t n = fa.size() < fb.size() ? fa.size() : fb.size();
    for (std::size_t i = 0; i < n; ++i) {
        // The unit count is visited before the units, so lists of different
        // length differ at "units.size" before the field names drift apart.
        if (!same_field(fa[i].first, fb[i].first) || fa[i].second != fb[i].second) {
            return describe(fa[i].first) + ": " + std::to_string(fa[i].second) + " vs " +
                   std::to_string(fb[i].second);
        }
    }
    return fa.size() == fb.size() ? std::string{} : std::string{"(field count)"};
}

std::string dump(const World& w) {
    std::string out;
    visit_fields(w, [&](const Field& f, std::uint64_t v) {
        out += describe(f);
        out += ' ';
        out += std::to_string(v);
        out += '\n';
    });
    return out;
}

Replay record(std::uint64_t seed, std::uint32_t ticks, const OrderSource& orders) {
    Replay r;
    r.seed = seed;
    World w = make_world(seed);
    for (std::uint32_t t = 0; t < ticks; ++t) {
        r.commands.push_back(orders(w));
        step(w, r.commands.back());
        r.hashes.push_back(hash(w));
    }
    return r;
}

namespace {

World run(const Replay& r, std::size_t ticks, const AfterStep& after,
          std::vector<std::uint64_t>* hashes) {
    World w = make_world(r.seed);
    for (std::size_t t = 0; t < ticks && t < r.commands.size(); ++t) {
        step(w, r.commands[t]);
        if (after) after(w);
        if (hashes) hashes->push_back(hash(w));
    }
    return w;
}

}  // namespace

std::vector<std::uint64_t> play(const Replay& r, const AfterStep& after) {
    std::vector<std::uint64_t> hashes;
    run(r, r.commands.size(), after, &hashes);
    return hashes;
}

World state_at(const Replay& r, std::uint32_t ticks, const AfterStep& after) {
    return run(r, ticks, after, nullptr);
}

std::optional<std::uint32_t> first_divergence(const std::vector<std::uint64_t>& a,
                                              const std::vector<std::uint64_t>& b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (a[i] != b[i]) return static_cast<std::uint32_t>(i + 1);
    }
    if (a.size() != b.size()) return static_cast<std::uint32_t>(n + 1);
    return std::nullopt;
}

std::optional<Desync> locate(const Replay& r, const AfterStep& after) {
    const auto tick = first_divergence(r.hashes, play(r, after));
    if (!tick) return std::nullopt;
    return Desync{*tick, first_difference(state_at(r, *tick), state_at(r, *tick, after))};
}

// Format, one record per line:
//   lockstep-replay 1
//   seed <n>
//   ticks <n>
//   t <hash, 16 hex digits> <command count>      once per tick, then its commands:
//   c <player> <unit> <order> <target_x> <target_y> <target_id>
//   end
std::string to_text(const Replay& r) {
    std::string out = "lockstep-replay 1\nseed " + std::to_string(r.seed) + "\nticks " +
                      std::to_string(r.commands.size()) + "\n";
    char buf[64];
    for (std::size_t t = 0; t < r.commands.size(); ++t) {
        std::snprintf(buf, sizeof buf, "t %016" PRIx64 " %zu\n", r.hashes[t], r.commands[t].size());
        out += buf;
        for (const Command& c : r.commands[t]) {
            out += "c " + std::to_string(c.player) + " " + std::to_string(c.unit_id) + " " +
                   std::to_string(static_cast<int>(c.order)) + " " + std::to_string(c.target_x) + " " +
                   std::to_string(c.target_y) + " " + std::to_string(c.target_id) + "\n";
        }
    }
    out += "end\n";
    return out;
}

namespace {

class Reader {
public:
    explicit Reader(std::string_view text) : rest_(text) {}

    // Next whitespace-separated token on the current line; empty at line end.
    std::string_view token() {
        while (!rest_.empty() && rest_.front() == ' ') rest_.remove_prefix(1);
        std::size_t n = 0;
        while (n < rest_.size() && rest_[n] != ' ' && rest_[n] != '\n') ++n;
        const std::string_view tok = rest_.substr(0, n);
        rest_.remove_prefix(n);
        return tok;
    }
    bool end_line() {
        while (!rest_.empty() && rest_.front() == ' ') rest_.remove_prefix(1);
        if (rest_.empty() || rest_.front() != '\n') return false;
        rest_.remove_prefix(1);
        return true;
    }
    template <class T>
    bool number(T& out, int base = 10) {
        const std::string_view tok = token();
        const auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), out, base);
        return !tok.empty() && ec == std::errc{} && ptr == tok.data() + tok.size();
    }
    bool word(std::string_view w) { return token() == w; }
    bool done() const { return rest_.empty(); }

private:
    std::string_view rest_;
};

}  // namespace

std::optional<Replay> from_text(std::string_view text) {
    Reader in(text);
    Replay r;
    std::uint32_t version = 0, ticks = 0;
    if (!in.word("lockstep-replay") || !in.number(version) || version != 1 || !in.end_line()) return std::nullopt;
    if (!in.word("seed") || !in.number(r.seed) || !in.end_line()) return std::nullopt;
    if (!in.word("ticks") || !in.number(ticks) || !in.end_line()) return std::nullopt;
    for (std::uint32_t t = 0; t < ticks; ++t) {
        std::uint64_t h = 0;
        std::size_t count = 0;
        if (!in.word("t") || !in.number(h, 16) || !in.number(count) || !in.end_line()) return std::nullopt;
        r.hashes.push_back(h);
        auto& cmds = r.commands.emplace_back();
        for (std::size_t i = 0; i < count; ++i) {
            Command c;
            int order = 0;
            if (!in.word("c") || !in.number(c.player) || !in.number(c.unit_id) || !in.number(order) ||
                !in.number(c.target_x) || !in.number(c.target_y) || !in.number(c.target_id) || !in.end_line() ||
                order < 0 || order > 2) {
                return std::nullopt;
            }
            c.order = static_cast<Order>(order);
            cmds.push_back(c);
        }
    }
    if (!in.word("end") || !in.end_line() || !in.done()) return std::nullopt;
    return r;
}

}  // namespace lockstep
