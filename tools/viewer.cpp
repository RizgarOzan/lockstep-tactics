// Watch a seeded match: the map, the units and the state hash, ten ticks a
// second. Built only with -DLOCKSTEP_VIEWER=ON; runs natively or in the
// browser (Emscripten).
//
//   Space pause · N next seed · R restart · Up/Down speed
//
// The viewer only reads the World. Unit positions are interpolated between
// ticks with float for smooth motion; that float never flows back into the
// simulation, so it cannot cause a desync (docs/notes/07-viewer.md).
#include <cinttypes>
#include <cstdio>
#include <vector>

#include "lockstep/random_orders.hpp"
#include "lockstep/world.hpp"
#include "raylib.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#endif

namespace {

constexpr int kTile = 40;
constexpr int kHud = 56;
constexpr int kWidth = lockstep::Map::kWidth * kTile;
constexpr int kHeight = lockstep::Map::kHeight * kTile + kHud;

struct Match {
    std::uint64_t seed = 1;
    std::uint64_t orders = 1;
    lockstep::World world;
    std::vector<lockstep::Unit> before;  // units at the previous tick, for interpolation
    float since_tick = 0.0f;
    float ticks_per_second = 10.0f;
    bool paused = false;

    void start(std::uint64_t s) {
        seed = s;
        orders = s;
        world = lockstep::make_world(s);
        before = world.units;
        since_tick = 0.0f;
    }
    void tick() {
        before = world.units;
        lockstep::step(world, lockstep::random_orders(world, orders));
    }
};

Match match;

float to_float(lockstep::Fixed v) { return static_cast<float>(v.raw) / 65536.0f; }

const lockstep::Unit* previous(std::uint32_t id) {
    for (const auto& u : match.before) {
        if (u.id == id) return &u;
    }
    return nullptr;
}

void draw() {
    BeginDrawing();
    ClearBackground(Color{24, 26, 31, 255});

    for (int y = 0; y < lockstep::Map::kHeight; ++y) {
        for (int x = 0; x < lockstep::Map::kWidth; ++x) {
            const bool wall = match.world.map.at(x, y) == lockstep::Tile::Wall;
            const Color c = wall ? Color{70, 74, 84, 255} : Color{38, 41, 48, 255};
            DrawRectangle(x * kTile, kHud + y * kTile, kTile - 1, kTile - 1, c);
        }
    }

    const float t = match.paused ? 1.0f : match.since_tick * match.ticks_per_second;
    const float blend = t < 1.0f ? t : 1.0f;
    const char* letters = "SAK";
    for (const auto& u : match.world.units) {
        const lockstep::Unit* p = previous(u.id);
        const float px = p ? to_float(p->x) : to_float(u.x);
        const float py = p ? to_float(p->y) : to_float(u.y);
        const float x = (px + (to_float(u.x) - px) * blend) * kTile;
        const float y = (py + (to_float(u.y) - py) * blend) * kTile + kHud;
        const Color team = u.owner == 0 ? Color{86, 156, 214, 255} : Color{214, 112, 86, 255};
        DrawCircle(static_cast<int>(x), static_cast<int>(y), kTile * 0.36f, team);
        const char letter[2] = {letters[static_cast<int>(u.type)], '\0'};
        DrawText(letter, static_cast<int>(x) - 5, static_cast<int>(y) - 9, 18, RAYWHITE);
        const float hp = static_cast<float>(u.hp) / static_cast<float>(lockstep::stats(u.type).max_hp);
        DrawRectangle(static_cast<int>(x) - 14, static_cast<int>(y) - 22, 28, 4, Color{20, 20, 20, 255});
        DrawRectangle(static_cast<int>(x) - 14, static_cast<int>(y) - 22, static_cast<int>(28 * hp), 4, GREEN);
    }

    char line[160];
    std::snprintf(line, sizeof line, "seed %" PRIu64 "   tick %u   hash %016" PRIx64 "%s", match.seed,
                  match.world.tick, lockstep::hash(match.world), match.paused ? "   (paused)" : "");
    DrawText(line, 10, 8, 18, RAYWHITE);
    DrawText("space pause   N next seed   R restart   up/down speed", 10, 32, 14, GRAY);
    EndDrawing();
}

void frame() {
    if (IsKeyPressed(KEY_SPACE)) match.paused = !match.paused;
    if (IsKeyPressed(KEY_N)) match.start(match.seed + 1);
    if (IsKeyPressed(KEY_R)) match.start(match.seed);
    if (IsKeyPressed(KEY_UP) && match.ticks_per_second < 60.0f) match.ticks_per_second *= 2.0f;
    if (IsKeyPressed(KEY_DOWN) && match.ticks_per_second > 1.25f) match.ticks_per_second /= 2.0f;

    if (!match.paused && match.world.tick < 600) {
        match.since_tick += GetFrameTime();
        while (match.since_tick >= 1.0f / match.ticks_per_second) {
            match.since_tick -= 1.0f / match.ticks_per_second;
            match.tick();
        }
    }
    draw();
}

}  // namespace

int main() {
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
