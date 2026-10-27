// Game/level_score.cpp - the scene score and its star rating (see level_score.h).
#include "Game/level_score.h"

#include <array>
#include <cmath>

namespace Game {

namespace {

// 0x7c7d88: the first word of each 8-byte entry, by scene index.
constexpr std::array<int32, 24> kParSeconds = {600, 600, 600, 900, 1200, 1200, 1200, 600, 1200, 900, 1200, 900,
                                               1200, 1500, 1500, 420, 1200, 1200, 1200, 900, 600, 600, 600, 600};

} // namespace

int32 parSeconds(int32 scene) noexcept
{
    return scene >= 0 && scene < static_cast<int32>(kParSeconds.size()) ? kParSeconds[static_cast<std::size_t>(scene)] : 0;
}

int32 styleStars(int32 points, float32 target, bool hard) noexcept
{
    const int32 goal = static_cast<int32>(std::lround(target));
    const float32 f = points < goal ? static_cast<float32>(points) / static_cast<float32>(goal) : 1.0f;
    if (hard)
        return f > 0.95f ? 4 : f > 0.75f ? 3 : f > 0.5f ? 2 : f > 0.25f ? 1 : 0;
    return f > 0.95f ? 3 : f > 0.66f ? 2 : f > 0.33f ? 1 : 0;
}

LevelResults levelResults(LevelScore score, int32 scene, bool hard) noexcept
{
    if (scene == 19)
    {
        score.kills -= 1;
        score.executions -= 2;
    }
    LevelResults r;
    r.score = score;
    r.scene = scene;
    r.hard = hard;
    r.timeBonus = score.timeMs / 1000 < parSeconds(scene);
    r.stars = styleStars(score.stylePoints, score.target(), hard) + (r.timeBonus ? 1 : 0);
    return r;
}

} // namespace Game
