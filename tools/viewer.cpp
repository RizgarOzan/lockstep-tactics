// Watch a match, or play Blue against the AI: board, unit cards, hits,
// deaths and the live state hash. Built only with -DLOCKSTEP_VIEWER=ON; runs
// natively or in the browser (Emscripten).
//
//   Buttons, or keys: Space pause · R restart · N next seed · Up/Down speed
//                     P play Blue / watch · S save the replay
//   Playing: click a blue unit (or its card), then a tile to move or an orange
//   unit to attack; right click clears the selection. Orders apply on the
//   next tick, so pausing to think is allowed.
//   viewer --capture <dir> <seed> <frames> <fps> [ticks/s]   render frames to PNG, hidden window
//
// In the browser the page can also connect two viewers (tools/shell.html):
// it calls net_start() once the WebRTC data channel is open, hands every
// frame from the peer to net_receive(), and net_send() goes the other way.
// The lockstep itself is lockstep::Lockstep (docs/notes/10-two-browsers.md).
//
// The viewer only reads the World; a player's click becomes an ordinary
// Command through lockstep::click() (docs/notes/08-player.md). Hits, attacks
// and deaths are worked out by comparing two consecutive ticks, and movement
// is interpolated with float; none of it flows back into the simulation
// (docs/notes/07-viewer.md). Every match is recorded as it plays, so "save"
// writes a replay that `lockstep verify` checks on any machine.
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include "lockstep/ai.hpp"
#include "lockstep/net.hpp"
#include "lockstep/player.hpp"
#include "lockstep/random_orders.hpp"
#include "lockstep/replay.hpp"
#include "lockstep/rollback.hpp"
#include "lockstep/world.hpp"
#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
// Hands a text to the browser as a file download.
EM_JS(void, download_text, (const char* text, const char* name), {
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([UTF8ToString(text)], {type : 'text/plain'}));
    a.download = UTF8ToString(name);
    a.click();
    URL.revokeObjectURL(a.href);
});
// A lockstep frame for the peer; the page sets window.lockstepNet on connect.
EM_JS(void, net_send, (const char* frame), {
    if (window.lockstepNet) window.lockstepNet.send(UTF8ToString(frame));
});
#else
void net_send(const char*) {}
#endif

namespace {

// Layout: a top bar, the board, and a panel of unit cards and buttons on the
// right. Text is drawn at 10 or 20 px only: raylib's default font is a 10 px
// bitmap, and whole multiples keep it crisp.
constexpr int kTile = 40;
constexpr int kTop = 56;
constexpr int kBoard = lockstep::Map::kWidth * kTile;
constexpr int kPanel = 260;
constexpr int kWidth = kBoard + kPanel;
constexpr int kHeight = kTop + lockstep::Map::kHeight * kTile;
constexpr std::uint32_t kMatchTicks = 600;
constexpr float kEffect = 0.35f;    // seconds a hit flash or attack line lasts
constexpr float kGhost = 0.8f;      // seconds a dead unit takes to fade
constexpr float kNextMatch = 3.0f;  // seconds a watched result stays up

const Color kBackground{16, 17, 21, 255};
const Color kBar{24, 26, 31, 255};
const Color kPanelBg{21, 23, 28, 255};
const Color kCard{30, 33, 40, 255};
const Color kCardHot{40, 44, 54, 255};
const Color kFloorA{37, 40, 48, 255};
const Color kFloorB{41, 44, 53, 255};
const Color kObjective{214, 178, 86, 255};  // the four tiles a side can win by holding
const Color kGrid{30, 32, 39, 255};
const Color kWall{70, 75, 90, 255};
const Color kWallTop{104, 110, 128, 255};
const Color kWallShade{44, 47, 58, 255};
const Color kText{226, 229, 235, 255};
const Color kMuted{132, 138, 152, 255};
const Color kTeam[2] = {Color{86, 156, 230, 255}, Color{236, 128, 82, 255}};
const Color kTeamDark[2] = {Color{34, 70, 116, 255}, Color{118, 52, 26, 255}};
const char* kTeamName[2] = {"BLUE", "ORANGE"};
const char* kTeamWord[2] = {"blue", "orange"};
const char* kTypeName[3] = {"Soldier", "Archer", "Knight"};
const char* kTypeGlyph[3] = {"S", "A", "K"};

struct Flash {
    std::uint32_t id;
    float left;
};
struct Shot {
    Vector2 from, to;
    std::uint8_t owner;
    float left;
};
struct Ghost {
    lockstep::Unit unit;
    float left;
};

struct Match {
    std::uint64_t seed = 1;
    std::uint64_t orders = 1;
    lockstep::World world;
    std::vector<lockstep::Unit> before;
    std::vector<Flash> flashes;
    std::vector<Shot> shots;
    std::vector<Ghost> ghosts;
    float since_tick = 0.0f;
    float ticks_per_second = 10.0f;
    float over_for = 0.0f;
    bool paused = false;
    bool human = false;                      // the local player is played by clicks, the rest by the AI
    std::uint32_t local_player = 0;          // Blue, unless an online match says otherwise
    std::uint32_t selected = 0;              // the local player's selected unit, 0 for none
    std::vector<lockstep::Command> pending;  // the local player's orders for the next tick
    lockstep::Replay replay;                 // the match so far, as `lockstep record` would write it
    // An online match: both players are people in two browsers, and a tick
    // runs only when both sides' orders for it have arrived.
    bool online = false;
    std::optional<lockstep::Lockstep> sync;    // input delay: a tick waits for both sides
    std::optional<lockstep::Rollback> roll;    // no delay: run now, re-run when the peer's orders arrive
    bool waiting = false;    // this tick is due but the peer's frame is not in yet
    bool peer_left = false;

    void start(std::uint64_t s) {
        seed = s;
        orders = s;
        world = lockstep::make_world(s);
        before = world.units;
        flashes.clear();
        shots.clear();
        ghosts.clear();
        since_tick = over_for = 0.0f;
        selected = 0;
        pending.clear();
        replay = {};
        replay.seed = s;
    }

    int alive(std::uint8_t owner) const {
        int n = 0;
        for (const auto& u : world.units) n += u.owner == owner;
        return n;
    }
    bool over() const { return world.tick >= kMatchTicks || lockstep::winner(world) != -1; }

    static const lockstep::Unit* find(const std::vector<lockstep::Unit>& units, std::uint32_t id) {
        for (const auto& u : units) {
            if (u.id == id) return &u;
        }
        return nullptr;
    }

    // A click on tile (tx, ty) while playing Blue. A newer order for a unit
    // replaces one still waiting, so what the player clicked last is what
    // the unit does.
    void board_click(int tx, int ty) {
        const lockstep::Click c = lockstep::click(world, local_player, selected, tx, ty);
        selected = c.selected;
        if (!c.command) return;
        std::erase_if(pending, [&](const lockstep::Command& p) { return p.unit_id == c.command->unit_id; });
        pending.push_back(*c.command);
    }

    // Returns false when an online tick has to wait for the peer.
    bool tick() {
        std::vector<lockstep::Command> commands;
        if (online && roll) {
            const std::string frame = roll->advance(pending);
            waiting = frame.empty();
            if (waiting) return false;
            pending.clear();
            net_send(frame.c_str());
            before = world.units;
            world = roll->world();
            if (selected != 0 && !find(world.units, selected)) selected = 0;
            effects();
            return true;
        }
        if (online) {
            const std::string frame = sync->submit(pending);
            if (!frame.empty()) {
                pending.clear();
                net_send(frame.c_str());
            }
            auto both = sync->next();
            waiting = !both.has_value();
            if (waiting) return false;
            commands = std::move(*both);
        } else {
            commands = lockstep::match_orders(world, orders);
            if (human) {
                std::erase_if(commands, [&](const lockstep::Command& c) { return c.player == local_player; });
                commands.insert(commands.end(), pending.begin(), pending.end());
                pending.clear();
            }
        }
        before = world.units;
        lockstep::step(world, commands);
        replay.commands.push_back(std::move(commands));
        replay.hashes.push_back(lockstep::hash(world));
        if (online) sync->ran(world.tick, replay.hashes.back());
        if (selected != 0 && !find(world.units, selected)) selected = 0;
        effects();
        return true;
    }

    // Flashes, attack lines and ghosts, read off `before` against `world`.
    void effects() {
        for (const auto& u : world.units) {
            const lockstep::Unit* p = find(before, u.id);
            if (p && u.hp < p->hp) flashes.push_back({u.id, kEffect});
            // A unit that attacked this tick has just had its cooldown reset.
            if (u.order == lockstep::Order::Attack && u.cooldown == lockstep::kAttackCooldown) {
                const lockstep::Unit* t = find(before, u.target_id);
                if (t) shots.push_back({centre(u), centre(*t), u.owner, kEffect});
            }
        }
        for (const auto& p : before) {
            if (!find(world.units, p.id)) ghosts.push_back({p, kGhost});
        }
    }

    // The peer's orders arrived (rollback): the present may have been re-run.
    void peer_frame(const char* frame) {
        if (roll) {
            roll->receive(frame);
            before = world.units;
            world = roll->world();
        } else if (sync) {
            sync->receive(frame);
        }
    }

    static Vector2 centre(const lockstep::Unit& u) {
        return {static_cast<float>(u.x.raw) / 65536.0f * kTile,
                static_cast<float>(u.y.raw) / 65536.0f * kTile + kTop};
    }

    void update(float dt) {
        for (auto& f : flashes) f.left -= dt;
        for (auto& s : shots) s.left -= dt;
        for (auto& g : ghosts) g.left -= dt;
        std::erase_if(flashes, [](const Flash& f) { return f.left <= 0; });
        std::erase_if(shots, [](const Shot& s) { return s.left <= 0; });
        std::erase_if(ghosts, [](const Ghost& g) { return g.left <= 0; });
        if (paused || peer_left) return;
        if (over()) {
            over_for += dt;
            // A watched match moves on by itself; a played one waits for R or N.
            if (over_for >= kNextMatch && !human) start(seed + 1);
            return;
        }
        since_tick += dt;
        // After a stall the ticks that are due run back to back, but no more
        // than half a second of them, so a long wait does not end in a blur.
        if (since_tick > 0.5f) since_tick = 0.5f;
        while (since_tick >= 1.0f / ticks_per_second && !over()) {
            if (!tick()) break;
            since_tick -= 1.0f / ticks_per_second;
        }
    }
};

Match match;

// Writes the match so far as a replay file: next to the executable natively,
// as a download in the browser.
void save_replay() {
    const std::string name = "replay-" + std::to_string(match.seed) + ".txt";
    // A rollback match saves its confirmed part: the ticks both sides' orders are in for.
    const std::string text = lockstep::to_text(match.roll ? match.roll->replay() : match.replay);
#if defined(__EMSCRIPTEN__)
    download_text(text.c_str(), name.c_str());
#else
    std::ofstream(name, std::ios::binary) << text;
    std::printf("saved %s (%zu ticks)\n", name.c_str(), match.replay.hashes.size());
#endif
}

Vector2 drawn_position(const lockstep::Unit& u) {
    const Vector2 now = Match::centre(u);
    const lockstep::Unit* p = Match::find(match.before, u.id);
    if (!p || match.paused || match.over()) return now;
    float t = match.since_tick * match.ticks_per_second;
    t = t < 1.0f ? t : 1.0f;
    const Vector2 then = Match::centre(*p);
    return {then.x + (now.x - then.x) * t, then.y + (now.y - then.y) * t};
}

Vector2 tile_centre(int tx, int ty) {
    return {(static_cast<float>(tx) + 0.5f) * kTile, kTop + (static_cast<float>(ty) + 0.5f) * kTile};
}

void text(const char* s, int x, int y, int size, Color c) { DrawText(s, x, y, size, c); }
void text_right(const char* s, int right, int y, int size, Color c) { DrawText(s, right - MeasureText(s, size), y, size, c); }
void text_centre(const char* s, int cx, int y, int size, Color c) { DrawText(s, cx - MeasureText(s, size) / 2, y, size, c); }

Color hp_colour(float hp) {
    return hp > 0.5f ? Color{112, 212, 118, 255} : hp > 0.25f ? Color{236, 198, 78, 255} : Color{232, 86, 78, 255};
}

void hp_bar(int x, int y, int w, int h, float hp) {
    DrawRectangle(x, y, w, h, Color{10, 11, 13, 255});
    DrawRectangle(x + 1, y + 1, static_cast<int>(static_cast<float>(w - 2) * hp), h - 2, hp_colour(hp));
}

// A unit: drop shadow, the type's shape in the team colour, its glyph, and
// its health underneath. `scale` and `alpha` fade a ghost out.
void draw_unit(const lockstep::Unit& u, Vector2 at, float alpha, float scale, bool flash) {
    const Color fill = flash ? RAYWHITE : Fade(kTeam[u.owner], alpha);
    const Color edge = Fade(kTeamDark[u.owner], alpha);
    const Color shadow = Fade(BLACK, 0.35f * alpha);
    const float r = kTile * 0.33f * scale;
    const int type = static_cast<int>(u.type);
    switch (u.type) {
        case lockstep::UnitType::Soldier:
            DrawEllipse(static_cast<int>(at.x), static_cast<int>(at.y + r * 0.9f), r * 1.1f, r * 0.45f, shadow);
            DrawCircleV(at, r + 2.5f, edge);
            DrawCircleV(at, r, fill);
            break;
        case lockstep::UnitType::Archer: {
            DrawEllipse(static_cast<int>(at.x), static_cast<int>(at.y + r * 0.9f), r * 1.1f, r * 0.45f, shadow);
            const Vector2 a{at.x, at.y - r - 3}, b{at.x - r - 3.5f, at.y + r * 0.8f + 2.5f}, c{at.x + r + 3.5f, at.y + r * 0.8f + 2.5f};
            DrawTriangle(a, b, c, edge);
            DrawTriangle({at.x, at.y - r + 1}, {at.x - r + 1, at.y + r * 0.8f}, {at.x + r - 1, at.y + r * 0.8f}, fill);
            break;
        }
        case lockstep::UnitType::Knight:
            DrawRectangleRounded({at.x - r * 1.1f, at.y + r * 0.5f, 2.2f * r, r * 0.9f}, 0.5f, 6, shadow);
            DrawRectangleRounded({at.x - r - 2.5f, at.y - r - 2.5f, 2 * r + 5, 2 * r + 5}, 0.3f, 4, edge);
            DrawRectangleRounded({at.x - r, at.y - r, 2 * r, 2 * r}, 0.3f, 4, fill);
            break;
    }
    if (alpha >= 1.0f) {
        const Color ink = flash ? kTeamDark[u.owner] : Fade(WHITE, 0.92f);
        text_centre(kTypeGlyph[type], static_cast<int>(at.x) + 1, static_cast<int>(at.y) - 9 + (type == 1 ? 3 : 0), 20, ink);
        hp_bar(static_cast<int>(at.x) - 14, static_cast<int>(at.y + r) + 6, 28, 5,
               static_cast<float>(u.hp) / static_cast<float>(lockstep::stats(u.type).max_hp));
    }
}

// The tiles a unit will walk to reach (gx, gy): the same next_step() the
// simulation uses, with the other units as walls, followed tile by tile
// until the goal or 40 steps. Drawn as dots, so the detour round a wall is
// visible before the unit takes it. Read-only; nothing here touches the World.
void draw_path(const lockstep::Unit& u, int gx, int gy, Color c) {
    lockstep::Map blocked = match.world.map;
    for (const auto& o : match.world.units) {
        const int ox = o.x.raw >> lockstep::Fixed::kFracBits, oy = o.y.raw >> lockstep::Fixed::kFracBits;
        if (o.id != u.id && (ox != gx || oy != gy)) blocked.set(ox, oy, lockstep::Tile::Wall);
    }
    int x = u.x.raw >> lockstep::Fixed::kFracBits, y = u.y.raw >> lockstep::Fixed::kFracBits;
    if (lockstep::line_clear(blocked, x, y, gx, gy)) return;  // a straight walk needs no dots
    for (int i = 0; i < 40; ++i) {
        const auto next = lockstep::next_step(blocked, x, y, gx, gy);
        if (!next) break;
        x = next->first;
        y = next->second;
        if (x == gx && y == gy) break;
        DrawCircleV(tile_centre(x, y), 3.0f, Fade(c, 0.55f));
    }
}

// Where an order points: a diamond on the destination tile, or a ring around
// the target unit, with the path there dotted. `alpha` tells an order still
// waiting from one already given.
void draw_order(const lockstep::Unit& u, lockstep::Order order, std::int32_t tx, std::int32_t ty,
                std::uint32_t target_id, float alpha) {
    const Color c = Fade(kTeam[u.owner], alpha);
    const Vector2 from = drawn_position(u);
    if (order == lockstep::Order::Move) {
        const Vector2 to = tile_centre(tx, ty);
        DrawLineEx(from, to, 1.5f, Fade(c, 0.3f));
        draw_path(u, tx, ty, c);
        DrawPoly(to, 4, kTile * 0.22f, 0.0f, c);
    } else if (order == lockstep::Order::Attack) {
        if (const lockstep::Unit* t = Match::find(match.world.units, target_id)) {
            const Vector2 to = drawn_position(*t);
            DrawLineEx(from, to, 1.5f, Fade(c, 0.3f));
            draw_path(u, t->x.raw >> lockstep::Fixed::kFracBits, t->y.raw >> lockstep::Fixed::kFracBits, c);
            DrawRing(to, kTile * 0.42f, kTile * 0.48f, 0, 360, 24, c);
        }
    }
}

// Selection ring, hovered tile, the selected unit's current order, every
// waiting order, and a one-line hint. Only while playing Blue.
void draw_player_layer(Vector2 mouse) {
    // Hovering any unit, in either mode, shows its order and the path it will
    // take: the AI's intentions are readable the same way the player's are.
    if (mouse.y >= kTop && mouse.x >= 0 && mouse.x < kBoard && mouse.y < kHeight) {
        const int tx = static_cast<int>(mouse.x / kTile), ty = static_cast<int>((mouse.y - kTop) / kTile);
        if (const lockstep::Unit* h = lockstep::unit_at(match.world, tx, ty)) {
            if (h->id != match.selected) draw_order(*h, h->order, h->target_x, h->target_y, h->target_id, 0.5f);
        }
        if (match.human) {
            DrawRectangleLinesEx({static_cast<float>(tx * kTile), static_cast<float>(kTop + ty * kTile), kTile, kTile}, 2,
                                 Fade(RAYWHITE, 0.35f));
        }
    }
    if (!match.human) return;
    const lockstep::Unit* sel = Match::find(match.world.units, match.selected);
    if (sel) {
        DrawRing(drawn_position(*sel), kTile * 0.44f, kTile * 0.49f, 0, 360, 32, RAYWHITE);
        draw_order(*sel, sel->order, sel->target_x, sel->target_y, sel->target_id, 0.6f);
    }
    for (const auto& p : match.pending) {
        if (const lockstep::Unit* u = Match::find(match.world.units, p.unit_id)) {
            draw_order(*u, p.order, p.target_x, p.target_y, p.target_id, 1.0f);
        }
    }
    if (!match.over()) {
        char hint[96];
        if (sel) {
            std::snprintf(hint, sizeof hint, "click a tile to move, an %s unit to attack", kTeamWord[1 - match.local_player]);
        } else {
            std::snprintf(hint, sizeof hint, "click a %s unit, or its card", kTeamWord[match.local_player]);
        }
        DrawRectangle(8, kTop + 8, MeasureText(hint, 10) + 16, 20, Fade(BLACK, 0.6f));
        text(hint, 16, kTop + 13, 10, kText);
    }
}

struct Button {
    Rectangle r;
    const char* label;
    bool primary = false;
};

bool button(const Button& b, bool clicked, Vector2 mouse) {
    const bool hot = CheckCollisionPointRec(mouse, b.r);
    const Color face = b.primary ? (hot ? Color{104, 170, 240, 255} : kTeam[0]) : (hot ? kCardHot : kCard);
    DrawRectangleRounded({b.r.x, b.r.y + 2, b.r.width, b.r.height}, 0.3f, 6, Color{10, 11, 13, 255});
    DrawRectangleRounded(b.r, 0.3f, 6, face);
    text_centre(b.label, static_cast<int>(b.r.x + b.r.width / 2), static_cast<int>(b.r.y + (b.r.height - 20) / 2), 20,
                b.primary ? Color{12, 20, 34, 255} : kText);
    return hot && clicked;
}

void draw_top_bar() {
    DrawRectangle(0, 0, kWidth, kTop, kBar);
    DrawRectangle(0, kTop - 1, kWidth, 1, Color{40, 43, 52, 255});
    text("LOCKSTEP", 16, 10, 20, kText);
    text("TACTICS", 16, 32, 10, kMuted);
    char line[96];
    std::snprintf(line, sizeof line, "seed %" PRIu64, match.seed);
    text(line, 140, 10, 20, kText);
    std::snprintf(line, sizeof line, "tick %u / %u", match.world.tick, kMatchTicks);
    text(line, 140, 34, 10, kMuted);
    std::snprintf(line, sizeof line, "%016" PRIx64, lockstep::hash(match.world));
    text_right(line, kWidth - 16, 10, 20, kText);
    text_right("state hash, FNV-1a over every field", kWidth - 16, 34, 10, kMuted);
    const float progress = static_cast<float>(match.world.tick) / kMatchTicks;
    DrawRectangle(300, 12, 220, 4, Color{40, 43, 52, 255});
    DrawRectangle(300, 12, static_cast<int>(220 * progress), 4, Color{150, 160, 180, 255});
    // Each side's ticks on the objective, towards the hold that wins.
    text("hold", 272, 19, 10, kMuted);
    for (int p = 0; p < 2; ++p) {
        const int y = 20 + p * 5;
        const float held = static_cast<float>(std::min(match.world.held[static_cast<std::size_t>(p)], lockstep::kHoldToWin)) / lockstep::kHoldToWin;
        DrawRectangle(300, y, 220, 3, Color{40, 43, 52, 255});
        DrawRectangle(300, y, static_cast<int>(220 * held), 3, kTeam[p]);
    }
    if (match.online) {
        if (match.roll) {
            std::snprintf(line, sizeof line, "online  -  you are %s  -  rollback, confirmed %u, re-run %llu",
                          kTeamName[match.local_player], match.roll->confirmed(),
                          static_cast<unsigned long long>(match.roll->resimulated()));
        } else {
            std::snprintf(line, sizeof line, "online  -  you are %s  -  %u ticks ahead", kTeamName[match.local_player],
                          match.sync->delay());
        }
        text_centre(line, 410, 34, 10, kTeam[match.local_player]);
        const auto desync = match.roll ? match.roll->desync() : match.sync->desync();
        if (desync) {
            std::snprintf(line, sizeof line, "DESYNC AT TICK %u  -  save the replay", *desync);
            DrawRectangle(kBoard / 2 - 150, kTop + 6, 300, 20, Color{120, 30, 30, 230});
            text_centre(line, kBoard / 2, kTop + 11, 10, RAYWHITE);
        } else if (match.peer_left) {
            text_centre("the other player left", kBoard / 2, kTop + 11, 10, kMuted);
        } else if (match.waiting) {
            text_centre(match.roll ? "too far ahead of the other player, waiting" : "waiting for the other player",
                        kBoard / 2, kTop + 11, 10, kMuted);
        }
    } else if (match.paused) text_centre("PAUSED", 410, 34, 10, kMuted);
    else if (match.human) text_centre("you are Blue", 410, 34, 10, kTeam[0]);
    else text_centre("random vs AI", 410, 34, 10, kMuted);
}

void draw_board() {
    // Who stands on the objective alone this tick: that side's colour on it.
    bool there[2] = {false, false};
    for (const auto& u : match.world.units) {
        there[u.owner] |= lockstep::on_objective(u.x.raw >> lockstep::Fixed::kFracBits, u.y.raw >> lockstep::Fixed::kFracBits);
    }
    const int holder = there[0] != there[1] ? (there[0] ? 0 : 1) : -1;
    for (int y = 0; y < lockstep::Map::kHeight; ++y) {
        for (int x = 0; x < lockstep::Map::kWidth; ++x) {
            const int px = x * kTile, py = kTop + y * kTile;
            if (match.world.map.at(x, y) == lockstep::Tile::Wall) {
                DrawRectangle(px, py, kTile, kTile, kWall);
                DrawRectangle(px, py, kTile, 6, kWallTop);
                DrawRectangle(px, py + kTile - 4, kTile, 4, kWallShade);
                // A wall above an open tile throws a little shadow on it.
                if (y + 1 < lockstep::Map::kHeight && match.world.map.at(x, y + 1) == lockstep::Tile::Floor) {
                    DrawRectangle(px, py + kTile, kTile, 5, Fade(BLACK, 0.25f));
                }
            } else {
                DrawRectangle(px, py, kTile, kTile, (x + y) % 2 ? kFloorA : kFloorB);
                DrawRectangle(px, py, kTile, 1, kGrid);
                DrawRectangle(px, py, 1, kTile, kGrid);
                if (lockstep::on_objective(x, y)) {
                    const Color c = holder == 0 || holder == 1 ? kTeam[holder] : kObjective;
                    DrawRectangle(px, py, kTile, kTile, Fade(c, 0.18f));
                    DrawRectangleLines(px, py, kTile, kTile, Fade(c, 0.6f));
                }
            }
        }
    }
}

// One unit card: glyph, name, health and what the unit is doing. Returns
// whether it was clicked.
bool unit_card(const lockstep::Unit& u, int x, int y, bool selected, bool clicked, Vector2 mouse) {
    const Rectangle r{static_cast<float>(x), static_cast<float>(y), kPanel - 32, 44};
    const bool hot = match.human && u.owner == match.local_player && CheckCollisionPointRec(mouse, r);
    DrawRectangleRounded(r, 0.2f, 4, hot ? kCardHot : kCard);
    if (selected) DrawRectangleRoundedLinesEx(r, 0.2f, 4, 2, RAYWHITE);
    DrawRectangle(x, y + 6, 3, 32, kTeam[u.owner]);
    DrawCircle(x + 24, y + 22, 13, kTeamDark[u.owner]);
    DrawCircle(x + 24, y + 22, 11, kTeam[u.owner]);
    text_centre(kTypeGlyph[static_cast<int>(u.type)], x + 25, y + 12, 20, Fade(WHITE, 0.92f));
    text(kTypeName[static_cast<int>(u.type)], x + 46, y + 7, 20, kText);
    char line[48];
    std::snprintf(line, sizeof line, "%d/%d", u.hp, lockstep::stats(u.type).max_hp);
    text_right(line, x + kPanel - 40, y + 9, 10, kMuted);
    hp_bar(x + 46, y + 29, 90, 6, static_cast<float>(u.hp) / static_cast<float>(lockstep::stats(u.type).max_hp));
    const char* doing = "idle";
    if (u.order == lockstep::Order::Move) {
        std::snprintf(line, sizeof line, "to %d,%d", u.target_x, u.target_y);
        doing = line;
    } else if (u.order == lockstep::Order::Attack) {
        const lockstep::Unit* t = Match::find(match.world.units, u.target_id);
        if (t) {
            if (u.cooldown > 0) std::snprintf(line, sizeof line, "%s, %u", kTypeName[static_cast<int>(t->type)], u.cooldown);
            else std::snprintf(line, sizeof line, "%s", kTypeName[static_cast<int>(t->type)]);
            doing = line;
        }
    }
    text_right(doing, x + kPanel - 40, y + 28, 10, kMuted);
    return hot && clicked;
}

// Returns whether `clicked` was left for the board.
bool draw_panel(bool clicked, Vector2 mouse) {
    DrawRectangle(kBoard, kTop, kPanel, kHeight - kTop, kPanelBg);
    DrawRectangle(kBoard, kTop, 1, kHeight - kTop, Color{40, 43, 52, 255});
    const int x = kBoard + 16;
    int y = kTop + 14;
    char line[64];
    for (std::uint8_t team = 0; team < 2; ++team) {
        text(kTeamName[team], x, y, 10, kTeam[team]);
        std::snprintf(line, sizeof line, "%d left", match.alive(team));
        text_right(line, x + kPanel - 32, y, 10, kMuted);
        y += 16;
        int shown = 0;
        for (const auto& u : match.world.units) {
            if (u.owner != team) continue;
            if (unit_card(u, x, y, match.human && u.id == match.selected, clicked, mouse)) {
                match.selected = u.id;
                clicked = false;
            }
            y += 50;
            ++shown;
        }
        for (; shown < 3; ++shown) {
            DrawRectangleRoundedLinesEx({static_cast<float>(x), static_cast<float>(y), kPanel - 32, 44}, 0.2f, 4, 1,
                                        Color{40, 43, 52, 255});
            text_centre("fallen", x + (kPanel - 32) / 2, y + 17, 10, Color{70, 74, 86, 255});
            y += 50;
        }
        y += 10;
    }

    const float bw = (kPanel - 32 - 8) / 2.0f;
    char speed[16];
    std::snprintf(speed, sizeof speed, "speed x%g", match.ticks_per_second / 10.0f);
    const auto at = [&](int col, int row) {
        return Rectangle{static_cast<float>(x) + static_cast<float>(col) * (bw + 8), static_cast<float>(y + row * 40), bw, 32};
    };
    const Button pause{at(0, 0), match.paused ? "play" : "pause"};
    const Button save{at(1, 0), "save"};
    if (button(pause, clicked, mouse)) match.paused = !match.paused;
    if (button(save, clicked, mouse)) save_replay();
    if (match.online) {
        // Seed, speed and sides are fixed by the connection; only pause and save remain.
        text(match.peer_left ? "the other player left; reload to play again" : "online match: seed and speed are shared", x,
             y + 46, 10, kMuted);
    } else {
        const Button restart{at(0, 1), "restart"};
        const Button next{at(1, 1), "next seed"};
        const Button faster{at(0, 2), speed};
        const Button mode{at(1, 2), match.human ? "watch" : "play Blue", !match.human};
        if (button(restart, clicked, mouse)) match.start(match.seed);
        if (button(next, clicked, mouse)) match.start(match.seed + 1);
        if (button(faster, clicked, mouse)) match.ticks_per_second = match.ticks_per_second >= 40.0f ? 5.0f : match.ticks_per_second * 2.0f;
        if (button(mode, clicked, mouse)) match.human = !match.human;
    }
    y += 3 * 40 + 6;
    text("Soldier beats Archer beats Knight", x, y, 10, kMuted);
    text("beats Soldier. Archers shoot 4 tiles.", x, y + 14, 10, kMuted);
    text("space pause  n next  r restart", x, y + 34, 10, Color{80, 85, 98, 255});
    text("up/down speed  p play  s save", x, y + 48, 10, Color{80, 85, 98, 255});
#if defined(LOCKSTEP_DEBUG_INPUT)
    static int presses = 0;
    presses += clicked;
    std::snprintf(line, sizeof line, "DBG %.0f,%.0f down %d touch %d presses %d", mouse.x, mouse.y,
                  IsMouseButtonDown(MOUSE_BUTTON_LEFT), GetTouchPointCount(), presses);
    text(line, x, kHeight - 16, 10, YELLOW);
#endif
    return clicked && mouse.x < kBoard && mouse.y >= kTop;
}

void draw(bool clicked, Vector2 mouse) {
    ClearBackground(kBackground);
    draw_board();
    for (const auto& g : match.ghosts) {
        const float t = g.left / kGhost;
        draw_unit(g.unit, Match::centre(g.unit), t * 0.8f, 0.6f + 0.4f * t, false);
    }
    for (const auto& s : match.shots) {
        DrawLineEx(s.from, s.to, 3.0f, Fade(kTeam[s.owner], s.left / kEffect));
    }
    for (const auto& u : match.world.units) {
        bool flash = false;
        for (const auto& f : match.flashes) flash |= f.id == u.id && f.left > kEffect * 0.5f;
        draw_unit(u, drawn_position(u), 1.0f, 1.0f, flash);
    }
    draw_player_layer(mouse);
    if (match.over()) {
        const int a = match.alive(0), b = match.alive(1);
        int win = lockstep::winner(match.world);
        if (win == -1) win = a == b ? 2 : a > b ? 0 : 1;  // out of ticks: more units left
        const bool by_hold = win != 2 && match.world.held[static_cast<std::size_t>(win)] >= lockstep::kHoldToWin;
        const char* line = win == 2 ? "DRAW" : win == 0 ? (by_hold ? "BLUE HOLDS THE OBJECTIVE" : "BLUE WINS")
                                             : (by_hold ? "ORANGE HOLDS THE OBJECTIVE" : "ORANGE WINS");
        const Color c = win == 2 ? RAYWHITE : kTeam[win];
        DrawRectangle(0, kTop + 270, kBoard, 100, Fade(BLACK, 0.7f));
        DrawRectangle(0, kTop + 270, kBoard, 2, c);
        text_centre(line, kBoard / 2, kTop + 294, 40, c);
        char sub[64];
        std::snprintf(sub, sizeof sub, "tick %u  -  %s", match.world.tick,
                      match.online ? "reload the page to play again"
                      : match.human ? "restart or next seed to play again"
                                    : "next match in a moment");
        text_centre(sub, kBoard / 2, kTop + 344, 10, kMuted);
    }
    draw_top_bar();
    // The panel takes the click first; what it does not use is a board click.
    if (draw_panel(clicked, mouse) && match.human && !match.over()) {
        match.board_click(static_cast<int>(mouse.x / kTile), static_cast<int>((mouse.y - kTop) / kTile));
    }
}

void frame() {
    if (IsKeyPressed(KEY_SPACE)) match.paused = !match.paused;
    if (!match.online) {
        if (IsKeyPressed(KEY_N)) match.start(match.seed + 1);
        if (IsKeyPressed(KEY_R)) match.start(match.seed);
        if (IsKeyPressed(KEY_UP) && match.ticks_per_second < 40.0f) match.ticks_per_second *= 2.0f;
        if (IsKeyPressed(KEY_DOWN) && match.ticks_per_second > 5.0f) match.ticks_per_second /= 2.0f;
        if (IsKeyPressed(KEY_P)) match.human = !match.human;
    }
    if (IsKeyPressed(KEY_S)) save_replay();
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) match.selected = 0;
    match.update(GetFrameTime());
    // A press is the frame the pointer goes down, whether it is a mouse button
    // or a finger. raylib's web backend maps a touch to the mouse *position*
    // but not to the mouse *button*, so IsMouseButtonPressed() never fires on
    // a phone; OR-ing the two states and taking the rising edge covers both
    // without counting a mouse click twice.
    static bool was_down = false;
    const bool down = IsMouseButtonDown(MOUSE_BUTTON_LEFT) || GetTouchPointCount() > 0;
    const bool clicked = down && !was_down;
    was_down = down;
    BeginDrawing();
    draw(clicked, GetMousePosition());
    EndDrawing();
}

// Renders `frames` frames at a fixed `fps` into <dir>/frame_NNNN.png with a
// hidden window, so README media can be rebuilt without anyone watching.
int capture(const char* dir, std::uint64_t seed, int frames, int fps, float ticks_per_second) {
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(kWidth, kHeight, "lockstep-tactics");
    // Draw into a texture rather than reading the screen: raylib batches draw
    // calls, so the screen can still be empty when it is read back.
    const RenderTexture2D target = LoadRenderTexture(kWidth, kHeight);
    match.start(seed);
    match.ticks_per_second = ticks_per_second;
    for (int i = 0; i < frames; ++i) {
        match.update(1.0f / static_cast<float>(fps));
        BeginTextureMode(target);
        draw(false, Vector2{-1, -1});
        EndTextureMode();
        Image shot = LoadImageFromTexture(target.texture);
        ImageFlipVertical(&shot);  // OpenGL textures are stored bottom-up
        const std::string path = std::string(dir) + "/frame_" + std::to_string(10000 + i).substr(1) + ".png";
        ExportImage(shot, path.c_str());
        UnloadImage(shot);
    }
    UnloadRenderTexture(target);
    CloseWindow();
    return 0;
}

}  // namespace

#if defined(__EMSCRIPTEN__)
extern "C" {
// The page calls these once its WebRTC data channel is up (tools/shell.html).
// delay 0 means rollback: orders run on the tick they are given.
EMSCRIPTEN_KEEPALIVE void net_start(int player, unsigned seed, unsigned delay) {
    match.start(seed);
    match.human = true;
    match.online = true;
    match.peer_left = false;
    match.local_player = static_cast<std::uint32_t>(player);
    match.sync.reset();
    match.roll.reset();
    if (delay == 0) match.roll.emplace(match.local_player, seed, 30u);
    else match.sync.emplace(match.local_player, delay);
    match.ticks_per_second = 10.0f;
    match.paused = false;
}
EMSCRIPTEN_KEEPALIVE void net_receive(const char* frame) { match.peer_frame(frame); }
EMSCRIPTEN_KEEPALIVE void net_closed() { match.peer_left = true; }
}
#endif

int main(int argc, char** argv) {
    if ((argc == 6 || argc == 7) && std::string(argv[1]) == "--capture") {
        const float tps = argc == 7 ? static_cast<float>(std::atof(argv[6])) : 10.0f;
        return capture(argv[2], std::strtoull(argv[3], nullptr, 10), std::atoi(argv[4]), std::atoi(argv[5]), tps);
    }
    InitWindow(kWidth, kHeight, "lockstep-tactics");
    match.start(1);
#if defined(__EMSCRIPTEN__)
    emscripten_set_main_loop(frame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose()) frame();
    CloseWindow();
#endif
    return 0;
}
