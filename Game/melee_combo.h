// Game/melee_combo.h - the player's fist attacks: quick-punch combos and the held hard blow.
//
// Driven by WeaponTypeData.ini's `fists` record (see Game/ai/ai_tuning.h):
//   a tap starts Quick1; another tap inside the swing's fire window (FIRE_WINDOW_STARTS /
//   _LENGTHS) queues the next punch, which starts at the swing's CHAIN_POINT - Quick1 ->
//   Quick2 -> Quick3. The blow lands COMBAT_DELAYS into each swing. When a combo ends, the next
//   one may start after COMBO_PAUSES[step]. Holding the button kHardHold seconds swings Hard1.
// The hold time is our choice (the data has none). Pure logic: the caller supplies clip lengths.
#pragma once
#include "engine_types.h"

#include <functional>
#include <string>

#include "Game/ai/ai_tuning.h"

namespace Game {

class MeleeCombo {
public:
    static constexpr float32 kHardHold = 0.35f;

    struct Step {
        std::string startClip;             // a swing starts this frame: play this clip
        const MeleeAttack* landed = nullptr; // a blow lands this frame
    };

    void press() noexcept;
    void release() noexcept;
    // `canStart`: the player may begin a swing now (on the ground, not flinching).
    Step update(float32 dt, const MeleeWeapon& fists, bool canStart, const std::function<float32(const std::string&)>& clipLength);
    void cancel() noexcept; // hit or killed: drop the swing and any queued punch

    [[nodiscard]] bool busy() const noexcept { return swinging_; }
    [[nodiscard]] int32 comboStep() const noexcept { return step_; }
    [[nodiscard]] bool hard() const noexcept { return hard_; }
    [[nodiscard]] float32 swingTime() const noexcept { return t_; } // seconds into the swing

private:
    bool held_ = false;
    float32 heldTime_ = 0.0f;
    bool hardFired_ = false;
    bool tap_ = false;      // a tap waiting to be used
    bool hardWanted_ = false;

    bool swinging_ = false;
    bool hard_ = false;
    int32 step_ = -1;       // combo step of the current / last quick swing (0..2)
    float32 t_ = 0.0f;      // seconds into the swing
    float32 length_ = 0.0f; // the swing clip's length
    bool landed_ = false;
    bool chain_ = false;    // the next punch is queued
    float32 pause_ = 0.0f;  // seconds before a new combo may start
};

} // namespace Game
