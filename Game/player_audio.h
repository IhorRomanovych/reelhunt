// Game/player_audio.h - the player's own body sounds: his heartbeat and his breath (the exe
// 0x45fe00 / 0x45ffb0, called from his update 0x45f3xx every frame unless he is dead, +0x760 0x1b;
// played by his audio handler 0x44e6d0: 0x44dab0 / 0x44d4d0).
//
// The heartbeat (entity audio 0x18): beats alternate, the second 0.27 x s after the first
// (0x71129c) and the next first 0.72 x s after that (0x7112a0), s his stamina level (+0x84c), 0.7
// when below 0.7 (0x710654). Each is SFX_HEART (0x130: level 100, range 4 m) at its level times
// 2 - his share of HIT_POINTS left (0x7105f4), from his feet + 1.5 m (0x6b2a50), unpanned (0x168)
// and not through the world. While his health is under 0.1 of it (0x710658) or he is locked on to
// a victim (+0x478 3) the next beat is boosted (0x6b3398): a boost of 100 (0x6b33a8) added to its
// level (at most 127) and its range times boost / 100 x 3 (0x6b33a4, at least 1), the boost falling
// by 3 a beat (0x6b33a0).
//
// The breath (0x19 in, 0x1a out), only while his stamina level is at most 0.9 (0x7112d8): in,
// then out, each followed by a wait from the table 0x7112dc by his stamina's step (the first of
// 0.1 / 0.16 / 0.18 / 0.24 / 0.32 / 0.4 / 0.5 / 0.6 / 0.75 / 0.9 at or above it, 0x7112b4). Its
// sample by the same step (0x71132c / 0x711330): SFX_PLAYER_BREATHE_IN_1 + step / 2,
// SFX_PLAYER_BREATHE_OUT_1 + step / 2 (level 30, range 10 m), at that level times 1 - stamina, from
// his feet + 1.8 m, unpanned, not through the world, at 0.8 of the sample's rate less up to 1/256.
#pragma once
#include "engine_types.h"

#include <vector>

namespace Game {

class PlayerAudio {
public:
    struct Sound {
        int32 sample = -1;
        int32 level = 0;       // 0..127, before the fall-off
        float32 range = 0.0f;  // metres
        float32 height = 1.5f; // above his feet
        float32 rate = 1.0f;   // times the sample's rate (the breath's 0.8, less up to 1/256)
    };
    struct State {
        float32 timeMs = 0.0f;  // The game's clock
        float32 health = 1.0f;  // share of HIT_POINTS left
        float32 stamina = 1.0f; // 0..1
        bool lockedOn = false;
        bool dead = false;
    };
    // One frame: the sounds that start now.
    [[nodiscard]] std::vector<Sound> update(const State& s);
    void reset() noexcept { *this = PlayerAudio{}; }

    // The breath's step for a stamina level (0x45ff70).
    [[nodiscard]] static int32 breathStep(float32 stamina) noexcept;

private:
    bool secondBeat_ = false;
    float32 lastBeat_ = 0.0f;
    bool boostPending_ = false;
    float32 boost_ = 0.0f;
    bool breathOut_ = false;
    float32 nextBreath_ = 0.0f;
};

} // namespace Game
