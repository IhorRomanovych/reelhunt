// Game/litter.h - the litter: sheets of newspaper lying about and blown by the wind (the exe
// 0x5c7990 init, 0x5c7a90 update, 0x5c8e70 whirlwind, 0x5c8800 draw; GTA3's CRubbish).
//
// 32 sheets (0x7c45b4, 0x54 bytes each) on three lists: free, lying (state 1), moving (state 2).
// SwitchLitterOn(off) fades them out, on fades them in: an alpha stepping 5 a frame (0..255,
// 0x7c5230); at 0 nothing runs. Each frame (0x5c7a90; ours: ticks of 1/30 s):
// - Gone: 2 sheets a frame (frame & 15 picks the pair) lying beyond 20 m (flat) of the camera.
// - Laid: the first free sheet goes down 19..20 m from the camera, half the time within ±0.5 rad
//   of where it looks, else anywhere round; where a line from the camera's height down 20 m meets
//   ground facing up (normal y >= 0.5; not surface class 13), 0.1 m above it, turned at random.
//   As the scripted camera lets go (0x5ea2c0) 30 go down at once, 0..20 m out, and all 32 are
//   checked for distance.
// - Moving: over its time, through 16 steps of its animation (GTA3's: a horizontal fraction and a
//   height, by type), from its place towards its place + its drift, its height lerped to the
//   target and lifted by the height table times its lift; a whirl (type 2) turns it round the
//   whirl's centre (0.5 - cos(t pi) / 2) x turns x 6.28. It spins 2 rad a second. At the end it
//   lies at the target.
// - Whirlwind (0x5c8e70): wind over 0.15, frame & 127 == 0x4c: a point 5 m ahead of the camera,
//   ±6 m each way, with nothing in 100 m above it and at least 2 m clear in each of the four
//   flat directions (9 m tests, each no longer than the last clearance). Every lying sheet within
//   that clearance whirls: 10..12 s, lift 3..5 m, turns (1 - d / r) x 10 (at least 1).
// - Blown: 1 frame in 32 / 8 / 2 / every frame for wind under 0.1 / 0.4 / 0.7 / more, one sheet at
//   random, if lying, moves wind x 3 m along +x and +z over wind x 1.5 + 1 s, lifted 0.2 m (type
//   1), where a line from 3 m above the spot down 10 m meets upward ground within 1.5 m of its
//   height.
// The wind (0x5b003a): the weather's (CLOUDY 0.2, WINDY 1, RAINY 0.3, THUNDERSTORM 1, FOGGY 0,
// CLEAR 0.1, table 0x7b3220) times one less the shelter (0x7b3278).
// Drawn (0x5c8800): sheet i uses newspaper0(i / 8 + 1) from frontend_pc.txd; flat quads half
// 0.15 x 0.07, 0.23 x 0.17, 0.15 x 0.15, 0.15 x 0.15 by group, each side scaled by
// 0.7 + 0.1 x (i & 3) and 0.7 + 0.1 x ((i >> 1) & 3); tinted by a colour per sheet (tables
// 0x7c5468..0x7c54a8), alpha 100 faded out from 16 to 19 m, times the fade alpha / 256; no z write.
#pragma once
#include "engine_types.h"

#include <array>
#include <functional>
#include <optional>
#include <random>

#include "Math/vector.h"

namespace Game {

class Litter {
public:
    static constexpr int32 kSheets = 32;

    struct Hit {
        Math::Vector3 at{0.0f, 0.0f, 0.0f};
        Math::Vector3 normal{0.0f, 1.0f, 0.0f};
    };
    struct World {
        // The first solid surface on a -> b.
        std::function<std::optional<Hit>(const Math::Vector3&, const Math::Vector3&)> hit;
        Math::Vector3 camera{0.0f, 0.0f, 0.0f};
        Math::Vector3 cameraLook{0.0f, 0.0f, 1.0f};
        bool scripted = false; // the scripts hold the camera (0x5ea2c0)
        float32 wind = 0.0f;
        uint32 nowMs = 0;
    };
    struct Sheet {
        uint8 state = 0;                          // 0 free, 1 lying, 2 moving
        uint8 type = 0;                           // the animation: 1 blown, 2 whirled
        uint8 turns = 0;                          // a whirl's
        Math::Vector3 base{0.0f, 0.0f, 0.0f};     // where it lies
        Math::Vector3 moving{0.0f, 0.0f, 0.0f};   // where it is while it moves
        float32 targetY = 0.0f;
        Math::Vector3 whirl{0.0f, 0.0f, 0.0f};    // the whirl's centre (x, z)
        uint32 moveStart = 0, moveFor = 0;        // ms
        float32 lift = 0.0f, driftX = 0.0f, driftZ = 0.0f;
        float32 angle = 0.0f;
    };

    void update(float32 dt, const World& w, std::mt19937& rng);
    void setOn(bool on) noexcept { on_ = on; }

    [[nodiscard]] const std::array<Sheet, kSheets>& sheets() const noexcept { return sheets_; }
    [[nodiscard]] int32 alpha() const noexcept { return alpha_; } // 0..255
    // Where sheet `i` is drawn, and its alpha (0..255) from the camera at `camera`; 0: not drawn.
    [[nodiscard]] Math::Vector3 position(int32 i) const;
    [[nodiscard]] int32 drawAlpha(int32 i, const Math::Vector3& camera) const;

private:
    void tick(float32 dt, const World& w, std::mt19937& rng);
    void whirlwind(const World& w, std::mt19937& rng);

    std::array<Sheet, kSheets> sheets_{};
    bool on_ = true;
    int32 alpha_ = 255;
    bool wasScripted_ = false;
    uint32 frame_ = 0;
    float32 carry_ = 0.0f;
};

} // namespace Game
