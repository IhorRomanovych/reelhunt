// Game/stamina.cpp - the player's stamina. See stamina.h.
#include "Game/stamina.h"

#include <algorithm>

namespace Game {

void Stamina::update(float32 dt, bool sprinting, Pace pace) noexcept
{
    if (sprinting && canSprint())
    {
        wasSprinting_ = true;
        level_ -= dt / std::max(0.01f, t_.totalSprintTime);
        if (level_ <= 0.01f)
        {
            level_ = 0.0f;
            canSprint_ = false; // out of breath
        }
        return;
    }
    if (wasSprinting_)
    {
        wasSprinting_ = false;
        pause_ = t_.recoveryPause;
    }
    if (pause_ > 0.0f)
    {
        pause_ -= dt;
        return;
    }
    const float32 refill = pace == Pace::Running ? t_.recoveryRunning : pace == Pace::Moving ? t_.recoveryMoving : t_.recoveryStill;
    level_ = std::min(1.0f, level_ + dt / std::max(0.01f, refill));
    if (level_ >= 0.4f)
        canSprint_ = true;
}

} // namespace Game
