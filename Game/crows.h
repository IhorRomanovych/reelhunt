// Game/crows.h - the crows of the open sky (exe 0x5cfaa0 .. 0x5d1a50).
//
// Six slots (0x7c7c04, 0x3c bytes each), only while the player is out under the sky (the rain's
// shelter 0x7b3278 below 0.1; 0x5afa10). Each tick (0x5cfaa0, 30 a second; ours: ticks of 1/30 s):
// - Every 256th (frame & 255 == 6) one may fly in: 27 m from the camera (half the time where it
//   looks), 0..8 m above its height, heading for the point 8 m ahead of the camera at 3 m/s if
//   100 m that way is open; its wings beat every 1024 ms.
// - Every 128th (frame & 127 == 0x65), for a body (a ped flagged +0x338 & 0x4000000; ours: a dead
//   hunter) 22..30 m from the camera with open sky 100 m above it, every free slot gets a crow on
//   it: within 0.4 m, 0.2 m up, turned at random, its wings 512 ms.
// - One slot a tick (frame % 6) twitches 1 in 16 and goes beyond 30 m, or once behind the camera
//   when it flies at it.
// - On the ground (state 0), with the player within 5 m (or a lightning flash) it takes off after
//   0..511 ms: half the time (0x5d0b40) up and away along a free 30 m line (up to ten tries, its
//   climb 0.2..0.6 of its heading) at 10 m/s (state 1, easing in at 0.5 a second), else (0x5d0f60)
//   at the camera at 20 m/s (state 2, easing in at 1 a second).
// - A shot's line within 0.5 m of one kills it (0x5cf7e0 -> 0x5d1a50): squeal #230..234,
//   FXRAT1, blood2 0.7..0.9 m. (The rats die the same way, 0x5cf8c0.)
// Drawn (0x5d0fc0): the global CJ_CROW, bobbing 0.1 m with its beat while it flies; two wings
// each side made of two quads 0.5 m long, 0.25 m to each side of the body (chord 0.5 m), their
// joints at sin(beat + 1/6) and sin(beat) of the half span, unfolding over 400 ms after take-off,
// black, "cloud" from frontend_pc.txd, faded from 18 to 27 m.
#pragma once
#include "engine_types.h"

#include <array>
#include <functional>
#include <random>
#include <vector>

#include "Math/vector.h"

namespace Game {

class Crows {
public:
    static constexpr int32 kMax = 6;

    struct World {
        std::function<bool(const Math::Vector3&, const Math::Vector3&)> clear; // nothing solid between
        Math::Vector3 camera{0.0f, 0.0f, 0.0f};
        Math::Vector3 cameraLook{0.0f, 0.0f, 1.0f};
        Math::Vector3 player{0.0f, 0.0f, 0.0f};
        bool openSky = false;                  // shelter under 0.1 (0x7b3278)
        std::vector<Math::Vector3> bodies;
        bool lightning = false;
        uint32 nowMs = 0;
    };
    struct Crow {
        bool active = false;
        uint8 state = 0;                       // 0 on the ground, 1 flying off, 2 flying at the camera
        Math::Vector3 pos{0.0f, 0.0f, 0.0f}, vel{0.0f, 0.0f, 0.0f}, want{0.0f, 0.0f, 0.0f};
        float32 yaw = 0.0f;
        uint32 twitchAt = 0, goAt = 0;         // +0x2c, +0x30 (the take-off)
        uint32 beatMs = 1024;                  // +0x34
        bool twitched = false;
    };

    // Advance by `dt` seconds.
    void update(float32 dt, const World& w, std::mt19937& rng);
    // A shot along a -> b: the crows within 0.5 m of it die; their positions.
    std::vector<Math::Vector3> shoot(const Math::Vector3& a, const Math::Vector3& b);
    void clear() { crows_ = {}; count_ = 0; }

    [[nodiscard]] const std::array<Crow, kMax>& crows() const noexcept { return crows_; }
    [[nodiscard]] int32 count() const noexcept { return count_; }
    // The wing joints of crow `slot` now, in its own frame (x out to its right, y up): the elbow
    // and the tip; mirror x for its left wing. Zero when it is not flying.
    void wing(int32 slot, uint32 nowMs, float32& elbowX, float32& elbowY, float32& tipX, float32& tipY) const;
    // How far it bobs (0x5d0fc0).
    [[nodiscard]] float32 bob(int32 slot, uint32 nowMs) const;

private:
    void tick(float32 dt, const World& w, std::mt19937& rng);

    std::array<Crow, kMax> crows_{};
    int32 count_ = 0;
    uint32 frame_ = 0;
    float32 carry_ = 0.0f;
};

// The shortest distance from `p` to the segment a -> b.
[[nodiscard]] float32 distanceToSegment(const Math::Vector3& p, const Math::Vector3& a, const Math::Vector3& b);

} // namespace Game
