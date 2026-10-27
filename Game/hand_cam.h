// Game/hand_cam.h - the hand-held camera shake (exe 0x591840).
//
// The player's hand-cam block (+0x7f4, built by 0x58cae0): on or off (HandCamSetActive, 0x1ea;
// the execution camera turns it on with the defaults as it starts, 0x4b2dd0, and off as it ends,
// 0x4b2eb0), then two sets of four: the look's (+0x20 the kick period in frames, +0x24 the braking
// factor, +0x28 the kick size, +0x2c the spring, in degrees a frame) and the roll's (+0x30..+0x3c).
// Both default to 5, 5.0, 0.0004, 0.038 (0x58cae0). HandCamSetAll (0x1e4: period, braking, kick,
// spring and +0x40, which nothing reads) sets the look's and copies them to the roll's (0x57f5b0).
//
// Each frame the scripted or the execution camera films with it on (0x591840, from 0x58f5e0;
// ours: ticks of 1/30 s):
// - Yaw and pitch: where the real view points as seen from last frame's shaken view, in degrees.
//   Its sign pulls the turn rate by the spring, times the braking factor when the rate runs the
//   other way (a pull by sign, not by size); the offset grows by the rate.
// - Roll: the same, pulled back to level.
// - Kicks: 1 frame in `period` (rand % period == 1) the yaw and pitch rates each take
//   (rand % 2000 - rand % 2000) * kick; the roll by its own set.
// The shaken view is the real one turned by the yaw offset about its up, by the pitch about its
// right, then rolled about its line of sight (RwMatrixRotate, pre-concatenated).
#pragma once
#include "engine_types.h"

#include <random>

namespace Game {

class HandCam {
public:
    struct Set {
        int32 period = 5;          // a kick 1 frame in this many (0: none)
        float32 braking = 5.0f;    // the pull's factor against the rate
        float32 kick = 0.0004f;    // degrees a frame per unit of (rand % 2000 - rand % 2000)
        float32 spring = 0.038f;   // degrees a frame, each frame
    };

    // Advance by `dt` seconds with the real view at `yaw` / `pitch` (radians).
    void update(float32 dt, float32 yaw, float32 pitch, const Set& look, const Set& roll, std::mt19937& rng);

    // The offsets to add to the real view (radians).
    [[nodiscard]] float32 yaw() const noexcept;
    [[nodiscard]] float32 pitch() const noexcept;
    [[nodiscard]] float32 roll() const noexcept;

private:
    void tick(float32 yaw, float32 pitch, const Set& look, const Set& roll, std::mt19937& rng);

    float32 yawRate_ = 0.0f, pitchRate_ = 0.0f, rollRate_ = 0.0f; // +0x0c, +0x08, +0x10 (deg/frame)
    float32 yaw_ = 0.0f, pitch_ = 0.0f, roll_ = 0.0f;             // +0x18, +0x14, +0x1c (deg)
    float32 lastYaw_ = 0.0f, lastPitch_ = 0.0f;                   // the real view last frame (deg)
    bool started_ = false;
    float32 carry_ = 0.0f;
};

} // namespace Game
