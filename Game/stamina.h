// Game/stamina.h - the player's stamina: what sprinting uses up (exe 0x4626c0, 0x466e10).
//
// A 0..1 level (player +0x84c, shown by the HUD's stamina bar). Sprinting drains it by
// dt / STAMINA_TOTAL_SPRINT_TIME (player record; 20 s). At 0.01 it is empty: the sprint stops and
// he cannot sprint again until it is back to 0.4 (the double at 0x7106c0). Not sprinting, after
// STAMINA_RECOVERY_PAUSE seconds (0), it refills by dt / STAMINA_RECOVERY_<RUNNING|MOVING|STILL>
// (80, 54, 22 s). The exe picks the recovery by his behaviour state; ours: RUNNING at his full
// pace, MOVING slower (walking, sneaking), STILL standing. STAMINA_NO_SPRINT_TIME (3 s) is read
// but its use is not traced.
#pragma once
#include "engine_types.h"

namespace Game {

struct StaminaTuning {
    float32 totalSprintTime = 20.0f;
    float32 noSprintTime = 3.0f;
    float32 recoveryRunning = 80.0f;
    float32 recoveryMoving = 54.0f;
    float32 recoveryStill = 22.0f;
    float32 recoveryPause = 0.0f;
};

class Stamina {
public:
    enum class Pace { Still, Moving, Running };

    void setTuning(const StaminaTuning& t) noexcept { t_ = t; }
    void reset() noexcept { level_ = 1.0f; canSprint_ = true; pause_ = 0.0f; }

    // Whether a held sprint is allowed now (not out of breath).
    [[nodiscard]] bool canSprint() const noexcept { return canSprint_ && level_ > 0.0f; }
    // One frame: he sprints (the sprint he was allowed), or moves at `pace`.
    void update(float32 dt, bool sprinting, Pace pace) noexcept;

    [[nodiscard]] float32 level() const noexcept { return level_; }

private:
    StaminaTuning t_;
    float32 level_ = 1.0f;
    bool canSprint_ = true;
    float32 pause_ = 0.0f;
    bool wasSprinting_ = false;
};

} // namespace Game
