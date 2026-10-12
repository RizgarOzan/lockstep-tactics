// Watch seeded matches: map, units, hits, deaths and the state hash. Built
// only with -DLOCKSTEP_VIEWER=ON; runs natively or in the browser (Emscripten).
//
//   Buttons, or keys: Space pause · R restart · N next seed · Up/Down speed
//   viewer --capture <dir> <seed> <frames> <fps> [ticks/s]   render frames to PNG, hidden window
//
// The viewer only reads the World. Hits, attacks and deaths are worked out by
// comparing two consecutive ticks, and movement is interpolated with float;
// none of it flows back into the simulation (docs/notes/07-viewer.md).
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"
#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace {

constexpr int kTile = 40;
constexpr int kHud = 96;
constexpr int kWidth = lockstep::Map::kWidth * kTile;
constexpr int kHeight = lockstep::Map::kHeight * kTile + kHud;
constexpr std::uint32_t kMatchTicks = 600;
constexpr float kEffect = 0.35f;     // seconds a hit flash or attack line lasts
constexpr float kGhost = 0.8f;       // seconds a dead unit takes to fade
constexpr float kNextMatch = 3.0f;   // seconds the result stays up

const Color kBackground{18, 20, 25, 255};
const Color kFloorA{36, 39, 46, 255};
const Color kFloorB{40, 43, 51, 255};
const Color kWall{74, 79, 92, 255};
const Color kWallTop{98, 104, 120, 255};
const Color kTeam[2] = {Color{86, 156, 230, 255}, Color{236, 128, 82, 255}};
const Color kTeamDark[2] = {Color{38, 78, 128, 255}, Color{128, 58, 30, 255}};
const char* kTeamName[2] = {"Blue", "Orange"};

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

    void start(std::uint64_t s) {
        seed = s;
        orders = s;
        world = lockstep::make_world(s);
        before = world.units;
        flashes.clear();
        shots.clear();
        ghosts.clear();
        since_tick = over_for = 0.0f;
    }

    int alive(std::uint8_t owner) const {
        int n = 0;
        for (const auto& u : world.units) n += u.owner == owner;
        return n;
    }
    bool over() const { return world.tick >= kMatchTicks || alive(0) == 0 || alive(1) == 0; }

    static const lockstep::Unit* find(const std::vector<lockstep::Unit>& units, std::uint32_t id) {
        for (const auto& u : units) {
            if (u.id == id) return &u;
        }
        return nullptr;
    }

    void tick() {
        before = world.units;
        lockstep::step(world, lockstep::random_orders(world, orders));
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

    static Vector2 centre(const lockstep::Unit& u) {
        return {static_cast<float>(u.x.raw) / 65536.0f * kTile,
                static_cast<float>(u.y.raw) / 65536.0f * kTile + kHud};
    }

    void update(float dt) {
        for (auto& f : flashes) f.left -= dt;
        for (auto& s : shots) s.left -= dt;
        for (auto& g : ghosts) g.left -= dt;
        std::erase_if(flashes, [](const Flash& f) { return f.left <= 0; });
        std::erase_if(shots, [](const Shot& s) { return s.left <= 0; });
        std::erase_if(ghosts, [](const Ghost& g) { return g.left <= 0; });
        if (paused) return;
        if (over()) {
            over_for += dt;
            if (over_for >= kNextMatch) start(seed + 1);
            return;
        }
        since_tick += dt;
        while (since_tick >= 1.0f / ticks_per_second && !over()) {
            since_tick -= 1.0f / ticks_per_second;
            tick();
        }
    }
};

Match match;

Vector2 drawn_position(const lockstep::Unit& u) {
    const Vector2 now = Match::centre(u);
    const lockstep::Unit* p = Match::find(match.before, u.id);
    if (!p || match.paused || match.over()) return now;
    float t = match.since_tick * match.ticks_per_second;
    t = t < 1.0f ? t : 1.0f;
    const Vector2 then = Match::centre(*p);
    return {then.x + (now.x - then.x) * t, then.y + (now.y - then.y) * t};
}

void draw_unit(const lockstep::Unit& u, Vector2 at, float alpha, float scale, bool flash) {
    const Color fill = flash ? RAYWHITE : Fade(kTeam[u.owner], alpha);
    const Color edge = Fade(kTeamDark[u.owner], alpha);
    const float r = kTile * 0.34f * scale;
    switch (u.type) {
        case lockstep::UnitType::Soldier:
            DrawCircleV(at, r + 2, edge);
            DrawCircleV(at, r, fill);
            break;
        case lockstep::UnitType::Archer: {
            const Vector2 a{at.x, at.y - r - 2}, b{at.x - r - 3, at.y + r * 0.8f + 2}, c{at.x + r + 3, at.y + r * 0.8f + 2};
            DrawTriangle(a, b, c, edge);
            DrawTriangle({at.x, at.y - r + 1}, {at.x - r + 1, at.y + r * 0.8f}, {at.x + r - 1, at.y + r * 0.8f}, fill);
            break;
        }
        case lockstep::UnitType::Knight:
            DrawRectangleRounded({at.x - r - 2, at.y - r - 2, 2 * r + 4, 2 * r + 4}, 0.3f, 4, edge);
            DrawRectangleRounded({at.x - r, at.y - r, 2 * r, 2 * r}, 0.3f, 4, fill);
            break;
    }
    if (alpha >= 1.0f) {
        const float hp = static_cast<float>(u.hp) / static_cast<float>(lockstep::stats(u.type).max_hp);
        const Color bar = hp > 0.5f ? Color{120, 220, 120, 255} : hp > 0.25f ? Color{235, 200, 80, 255} : Color{235, 90, 80, 255};
        DrawRectangle(static_cast<int>(at.x) - 15, static_cast<int>(at.y - r) - 10, 30, 5, Color{12, 12, 14, 255});
        DrawRectangle(static_cast<int>(at.x) - 15, static_cast<int>(at.y - r) - 10, static_cast<int>(30 * hp), 5, bar);
    }
}

struct Button {
    Rectangle r;
    const char* label;
};

bool button(const Button& b, bool clicked, Vector2 mouse) {
    const bool hot = CheckCollisionPointRec(mouse, b.r);
    DrawRectangleRounded(b.r, 0.35f, 6, hot ? Color{64, 70, 84, 255} : Color{44, 48, 58, 255});
    const int w = MeasureText(b.label, 16);
    DrawText(b.label, static_cast<int>(b.r.x + (b.r.width - w) / 2), static_cast<int>(b.r.y + 7), 16, RAYWHITE);
    return hot && clicked;
}

void draw_hud(bool clicked, Vector2 mouse) {
    DrawRectangle(0, 0, kWidth, kHud, Color{26, 28, 34, 255});
    char line[96];
    std::snprintf(line, sizeof line, "seed %" PRIu64, match.seed);
    DrawText(line, 14, 12, 20, RAYWHITE);
    for (std::uint8_t t = 0; t < 2; ++t) {
        const int x = 150 + t * 120;
        DrawCircle(x, 22, 7, kTeam[t]);
        std::snprintf(line, sizeof line, "%s %d", kTeamName[t], match.alive(t));
        DrawText(line, x + 12, 13, 18, LIGHTGRAY);
    }
    std::snprintf(line, sizeof line, "hash %016" PRIx64, lockstep::hash(match.world));
    DrawText(line, kWidth - MeasureText(line, 16) - 14, 15, 16, GRAY);

    const float progress = static_cast<float>(match.world.tick) / kMatchTicks;
    DrawRectangle(14, 40, kWidth - 28, 4, Color{44, 48, 58, 255});
    DrawRectangle(14, 40, static_cast<int>((kWidth - 28) * progress), 4, Color{150, 160, 180, 255});

    char speed[16];
    std::snprintf(speed, sizeof speed, "speed x%g", match.ticks_per_second / 10.0f);
    const Button pause{{14, 54, 96, 30}, match.paused ? "play" : "pause"};
    const Button restart{{118, 54, 96, 30}, "restart"};
    const Button next{{222, 54, 96, 30}, "next seed"};
    const Button faster{{326, 54, 110, 30}, speed};
    if (button(pause, clicked, mouse)) match.paused = !match.paused;
    if (button(restart, clicked, mouse)) match.start(match.seed);
    if (button(next, clicked, mouse)) match.start(match.seed + 1);
    if (button(faster, clicked, mouse)) match.ticks_per_second = match.ticks_per_second >= 40.0f ? 5.0f : match.ticks_per_second * 2.0f;
    std::snprintf(line, sizeof line, "tick %u / %u", match.world.tick, kMatchTicks);
    DrawText(line, kWidth - MeasureText(line, 16) - 14, 62, 16, GRAY);
#if defined(LOCKSTEP_DEBUG_INPUT)
    static int presses = 0;
    presses += clicked;
    std::snprintf(line, sizeof line, "DBG mouse %.0f,%.0f down %d touch %d presses %d", mouse.x, mouse.y,
                  IsMouseButtonDown(MOUSE_BUTTON_LEFT), GetTouchPointCount(), presses);
    DrawText(line, 14, kHud - 12, 10, YELLOW);
#endif
}

void draw(bool clicked, Vector2 mouse) {
    ClearBackground(kBackground);
    for (int y = 0; y < lockstep::Map::kHeight; ++y) {
        for (int x = 0; x < lockstep::Map::kWidth; ++x) {
            const int px = x * kTile, py = kHud + y * kTile;
            if (match.world.map.at(x, y) == lockstep::Tile::Wall) {
                DrawRectangle(px, py, kTile, kTile, kWall);
                DrawRectangle(px, py, kTile, 5, kWallTop);
            } else {
                DrawRectangle(px, py, kTile, kTile, (x + y) % 2 ? kFloorA : kFloorB);
            }
        }
    }
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
    if (match.over()) {
        const int a = match.alive(0), b = match.alive(1);
        const char* text = a == b ? "Draw" : a > b ? "Blue wins" : "Orange wins";
        const Color c = a == b ? RAYWHITE : kTeam[a > b ? 0 : 1];
        DrawRectangle(0, kHud + 260, kWidth, 80, Fade(BLACK, 0.6f));
        DrawText(text, (kWidth - MeasureText(text, 36)) / 2, kHud + 280, 36, c);
    }
    draw_hud(clicked, mouse);
}

void frame() {
    if (IsKeyPressed(KEY_SPACE)) match.paused = !match.paused;
    if (IsKeyPressed(KEY_N)) match.start(match.seed + 1);
    if (IsKeyPressed(KEY_R)) match.start(match.seed);
    if (IsKeyPressed(KEY_UP) && match.ticks_per_second < 40.0f) match.ticks_per_second *= 2.0f;
    if (IsKeyPressed(KEY_DOWN) && match.ticks_per_second > 5.0f) match.ticks_per_second /= 2.0f;
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
