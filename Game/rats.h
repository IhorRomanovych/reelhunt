// Game/rats.h - the rats that scurry about dark floors (exe 0x5cdc40 .. 0x5cf8c0).
//
// Up to 16 (0x7c7370, 0x6c bytes each), at most SetMaxNumberOfRats(n) at once (0x57e3f6:
// min(n, 16) into 0x7c7a34; 16 at level start, 0x5cdc40). The exe runs them each frame (0x5cdd10,
// at 30 a second; ours: ticks of 1/30 s):
// - Every 16th tick (frame & 15 == 6) one may appear 27 m from the camera, half the time within
//   +-0.5 rad of where it looks, else anywhere round it, on a floor found by a ray from 2 m above
//   the camera's height down 22 m, within 7 m of that height and dark there (its prelight's R + G
//   + B under 1.5). Its model is the global CJ_RAT.
// - Each tick one (frame & 15) is checked: 1 in 16 it twitches (+0x64, drawn hopping for 2 s),
//   and it goes when more than 30 m from the camera.
// - Each 64th tick (frame & 63 == 54) the nearest body (a ped flagged +0x338 & 0x4000000; ours: a
//   dead hunter) within 25 m of the camera draws them; so does the player once he is dead.
// - Standing (state 0): within 3 m of the living player it runs (state 2) 3 m straight away from
//   him, else to either side. Otherwise 1 in 64 ticks (or on a lightning flash) it runs to within
//   2.3 m (at most 3/4 of the way) of a body within 10 m; else at random runs towards the nearest
//   rat within 20 m, or walks (state 1) up to 3.8 m at random, keeping 3 m from the player.
// - A move (0x5cefd0) goes only to a floor within 1 m of its height (found from 1 m above the
//   target), dark enough unless it is running (R + G + B under 1.7, or no brighter than here),
//   with a clear line 0.3 m up and 0.2 m beyond. A walk takes 2 s, a run 0.6 s; it turns 0.4 rad a
//   tick towards where it goes.
// - Within 0.35 m of the living player it is squashed (0x5cf8c0): squeal #230..234, FXRAT1,
//   blood 0.7..0.9 m (Game/decals.h).
// Drawn (0x5cf3d0) 0.1 m up, scaled 1, 0.8, 1.1, 1.07, 0.7 by its slot, wobbling 0.1 rad every
// 128 ms while it moves, faded out from 22 to 27 m (ours: hidden beyond 27).
#pragma once
#include "engine_types.h"

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <random>
#include <vector>

#include "Math/vector.h"

namespace Game {

class Rats {
public:
    static constexpr int32 kMax = 16;

    // A floor under a point: its height and its prelight (and that light's R + G + B).
    struct Floor {
        float32 y = 0.0f;
        float32 light = 0.0f;
        Math::Vector3 colour{0.0f, 0.0f, 0.0f};
    };
    struct World {
        // The floor below (x, top, z) within `depth` metres, if any.
        std::function<std::optional<Floor>(float32 x, float32 top, float32 z, float32 depth)> floor;
        // Nothing solid on the segment.
        std::function<bool(const Math::Vector3&, const Math::Vector3&)> clear;
        Math::Vector3 camera{0.0f, 0.0f, 0.0f};
        float32 cameraYaw = 0.0f;              // atan2(look.x, look.z)
        Math::Vector3 player{0.0f, 0.0f, 0.0f};
        bool playerAlive = true;
        std::vector<Math::Vector3> bodies;     // dead hunters
        bool lightning = false;
        uint32 nowMs = 0;
        float32 spawnRange = 27.0f;            // (0x7c7a6c; REELHUNT_RATS_NEAR brings them close)
    };
    struct Rat {
        bool active = false;
        uint8 state = 0;                       // 0 standing, 1 walking, 2 running
        Math::Vector3 pos{0.0f, 0.0f, 0.0f}, from{0.0f, 0.0f, 0.0f}, to{0.0f, 0.0f, 0.0f};
        float32 light = 0.0f;                  // its floor's prelight (R + G + B)
        Math::Vector3 colour{0.0f, 0.0f, 0.0f}; // that prelight: it is lit by it (+0x24, 0x5cf3d0)
        float32 yaw = 0.0f, wantYaw = 0.0f;
        uint32 moveStart = 0, twitchAt = 0;
        bool twitched = false;
    };

    void setLimit(int32 n) noexcept { limit_ = std::clamp(n, 0, kMax); }
    [[nodiscard]] int32 limit() const noexcept { return limit_; }
    // Advance by `dt` seconds (in ticks of 1/30 s). Returns the rats squashed (their positions).
    std::vector<Math::Vector3> update(float32 dt, const World& w, std::mt19937& rng);
    // A shot along a -> b (0x5cf7e0): the rats within 0.5 m of it die; their positions.
    std::vector<Math::Vector3> shoot(const Math::Vector3& a, const Math::Vector3& b);
    void clear();

    [[nodiscard]] const std::array<Rat, kMax>& rats() const noexcept { return rats_; }
    [[nodiscard]] int32 count() const noexcept { return count_; }
    // The drawing (0x5cf3d0): its scale by slot, and its yaw now (wobbling while it moves).
    [[nodiscard]] static float32 scale(int32 slot) noexcept;
    [[nodiscard]] float32 drawYaw(int32 slot, uint32 nowMs) const noexcept;

    // 0x5cefd0: a move to `target` (1 walk, 2 run); false when it cannot go there.
    bool move(Rat& r, const Math::Vector3& target, uint8 mode, const World& w) const;

private:
    void tick(const World& w, std::mt19937& rng, std::vector<Math::Vector3>& squashed);
    void think(int32 slot, const World& w, std::mt19937& rng);

    std::array<Rat, kMax> rats_{};
    int32 count_ = 0;
    int32 limit_ = kMax;
    uint32 frame_ = 0;
    float32 carry_ = 0.0f;
    bool attract_ = false;
    Math::Vector3 attractAt_{0.0f, 0.0f, 0.0f};
};

} // namespace Game
