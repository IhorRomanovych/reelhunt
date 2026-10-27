// Game/melee_combo.cpp - the player's fist attacks. See melee_combo.h.
#include "Game/melee_combo.h"

#include <algorithm>
#include <string>

namespace Game {

namespace {

// The weapon's clips: <stem>_Stance_Fire_Quick1..3 and _Hard1 (Fist..., Blade...).
std::string quickClip(const MeleeWeapon& w, int32 step)
{
    return w.clipStem + "_Stance_Fire_Quick" + std::to_string(std::clamp(step, 0, 2) + 1);
}

std::string hardClip(const MeleeWeapon& w)
{
    return w.clipStem + "_Stance_Fire_Hard1";
}

} // namespace

void MeleeCombo::press() noexcept
{
    held_ = true;
    heldTime_ = 0.0f;
    hardFired_ = false;
}

void MeleeCombo::release() noexcept
{
    if (held_ && !hardFired_)
        tap_ = true;
    held_ = false;
}

void MeleeCombo::cancel() noexcept
{
    swinging_ = false;
    chain_ = false;
    tap_ = false;
    hardWanted_ = false;
    step_ = -1;
}

MeleeCombo::Step MeleeCombo::update(float32 dt, const MeleeWeapon& fists, bool canStart,
                                    const std::function<float32(const std::string&)>& clipLength)
{
    Step out;
    pause_ = std::max(0.0f, pause_ - dt);
    if (held_ && !hardFired_ && (heldTime_ += dt) >= kHardHold)
    {
        hardFired_ = true;
        hardWanted_ = true;
    }

    auto start = [&](const std::string& clip, bool hard, int32 step) {
        swinging_ = true;
        hard_ = hard;
        step_ = step;
        t_ = 0.0f;
        length_ = clipLength ? clipLength(clip) : 1.0f;
        if (length_ <= 0.0f)
            length_ = 1.0f;
        landed_ = false;
        chain_ = false;
        out.startClip = clip;
    };

    if (swinging_)
    {
        const MeleeAttack& a = hard_ ? fists.hard : fists.quick;
        t_ += dt;
        if (tap_)
        {
            // A tap inside the fire window queues the next punch of the combo.
            if (!hard_ && step_ < 2 && t_ >= a.fireWindowStart && t_ <= a.fireWindowStart + a.fireWindowLength)
                chain_ = true;
            tap_ = false;
        }
        if (!landed_ && t_ >= a.strikeAt)
        {
            landed_ = true;
            out.landed = &a;
        }
        if (chain_ && t_ >= a.chainPoint)
            start(quickClip(fists, step_ + 1), false, step_ + 1);
        else if (t_ >= length_)
        {
            swinging_ = false;
            pause_ = hard_ ? 0.0f : fists.comboPauses[static_cast<std::size_t>(std::clamp(step_, 0, 2))];
        }
        hardWanted_ = false; // a hold mid-swing does not queue a hard blow
        return out;
    }

    if (!canStart || pause_ > 0.0f)
    {
        tap_ = false;
        hardWanted_ = false;
        return out;
    }
    if (hardWanted_)
        start(hardClip(fists), true, -1);
    else if (tap_)
        start(quickClip(fists, 0), false, 0);
    tap_ = false;
    hardWanted_ = false;
    return out;
}

} // namespace Game
