// Game/level_score.h - a scene's score: the end-of-scene results (time, kills, executions,
// style points) and the star rating.
//
// The exe:
//   Style points (record 0x7b7e00, total 0x7b7e10; 0x5b6030): a hunter killed +2 (0x4ecdea, not
//   in an execution), an execution +4 / +5 / +6 for hasty / violent / gruesome (0x48120a), a head
//   burst +1 (0x49ccb0, Explode_Head).
//   Kills 0x7b7da0 (0x5b63a0), executions 0x7b7d84 (0x5b62e0).
//   SetNumberOfKillableHuntersInLevel(total, executable) (0x57ef3f -> 0x5d2430): the points a
//   perfect scene earns, 2 for each hunter that can only be killed and 6 for each that can be
//   executed (0x7c7e70 / 0x7c7e6c).
//   The rating (0x5d2140): points / target (1 when the target is reached or zero) to stars -
//   HARDCORE 0x5d2250: > 0.25, 0.5, 0.75, 0.95 -> 1..4; FETISH 0x5d22c0: > 0.33, 0.66, 0.95 ->
//   1..3 - and one more when the scene took less than its par time (0x7c7d88, 0x5d2110).
//   Scene index 19 (the last story scene) counts one kill and two executions less (0x5d20f0).
#pragma once
#include "engine_types.h"

namespace Game {

struct LevelScore {
    int32 timeMs = 0;       // the time the scene took (0x5db4c0)
    int32 kills = 0;        // hunters killed, executions not counted
    int32 executions = 0;
    int32 stylePoints = 0;
    int32 hunters = 0;      // SetNumberOfKillableHuntersInLevel
    int32 executable = 0;

    void kill() noexcept { ++kills; stylePoints += 2; }
    // `level` as ExecutionCharge: 2 hasty, 3 violent, 4 gruesome.
    void execution(int32 level) noexcept { ++executions; stylePoints += level + 2; }
    void headBurst() noexcept { stylePoints += 1; }
    void setHunters(int32 total, int32 canExecute) noexcept { hunters = total; executable = canExecute < total ? canExecute : total; }
    // The points a perfect scene earns (0x7c7ea0).
    [[nodiscard]] float32 target() const noexcept
    {
        return static_cast<float32>(hunters - executable) * 2.0f + static_cast<float32>(executable) * 6.0f;
    }
};

// A finished scene as the results screen shows it.
struct LevelResults {
    LevelScore score;
    int32 scene = -1;       // 0-based levels.txt index
    bool hard = false;      // HARDCORE
    int32 stars = 0;        // the rating, time bonus included (FINAL)
    bool timeBonus = false; // under par
};

// The par time of scene `scene` in seconds (0x7c7d88), 0 if none.
[[nodiscard]] int32 parSeconds(int32 scene) noexcept;
// Stars for `points` of `target` (0x5d2250 / 0x5d22c0), without the time bonus.
[[nodiscard]] int32 styleStars(int32 points, float32 target, bool hard) noexcept;
// The results of a finished scene (0x5d20f0, 0x5d2140).
[[nodiscard]] LevelResults levelResults(LevelScore score, int32 scene, bool hard) noexcept;

} // namespace Game
