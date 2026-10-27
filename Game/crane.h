// Game/crane.h - a pad-turned entity: Scrap2's crane cab (exe 0x4d4558).
//
// SetPadControlEntityRotation(entity, on) (0x2b6) flags it (+0xc8 & 0x400000) and the player
// (+0xc8 & 0x800). Each frame (0x4d4558; ours: ticks of 1/30 s) a moving mouse turns it by its x
// times -18 degrees a second (0x69b4ac; ours: not mapped), else the pad does through a turn rate
// (0x739204):
// - The buttons (0x10 left, 0x11 right): the rate steps 0.05 (0x739210) that way a frame; with
//   neither it falls back towards 0 by 0.05.
// - The stick (past 0.0001): the rate moves towards its x by at most 0.04 (0x73920c) a frame; held
//   at or inside the rate on its side, a rate under 0.6 decays by a tenth a frame.
// - The rate stays within +-2 (0x739208); the entity turns rate x -18 degrees a second (0x69b4b0)
//   about its up.
#pragma once
#include "engine_types.h"

#include "Math/vector.h"

namespace Game {

// A PC_HANGING physics object (0x490e90, vtable 0x724a68: Scrap2's magnet, HANGING_MAGNET_PHYS):
// it hangs from its pivot (LinkHangingEntToEnt) with its centre of mass CENTRE_OF_MASS below it
// (the magnet's 4 m), swinging under the same 25 m/s^2 as everything (0x48bb80) and losing
// SPIN_RESISTANCE a frame of its swing. Ours: a point pendulum of that length.
class Hanging {
public:
    void update(float32 dt, const Math::Vector3& pivot, float32 length, float32 spin);
    // A push on its centre of mass (the magnet's pull on what it draws, the other way).
    void push(const Math::Vector3& dv) noexcept { vel_ += dv; }
    // Which way it hangs from the pivot (unit; straight down at rest).
    [[nodiscard]] Math::Vector3 down() const noexcept { return down_; }

private:
    Math::Vector3 bob_{0.0f, 0.0f, 0.0f}, vel_{0.0f, 0.0f, 0.0f};
    Math::Vector3 down_{0.0f, -1.0f, 0.0f};
    bool started_ = false;
    float32 carry_ = 0.0f;
};

class CraneTurn {
public:
    // The turn (radians, about the vertical) for `dt` seconds with the buttons held.
    float32 update(float32 dt, bool left, bool right);
    [[nodiscard]] float32 rate() const noexcept { return rate_; }
    void reset() noexcept { rate_ = 0.0f; carry_ = 0.0f; }

private:
    float32 rate_ = 0.0f;
    float32 carry_ = 0.0f;
};

} // namespace Game
