// Game/player_audio.cpp - see player_audio.h.
#include "Game/player_audio.h"

#include "Game/sample_levels.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Game {

namespace {

constexpr int32 kHeart = 0x130;      // SFX_HEART
constexpr int32 kBreatheIn = 0x124;  // SFX_PLAYER_BREATHE_IN_1
constexpr int32 kBreatheOut = 0x12a; // SFX_PLAYER_BREATHE_OUT_1
constexpr float32 kSteps[10] = {0.1f, 0.16f, 0.18f, 0.24f, 0.32f, 0.4f, 0.5f, 0.6f, 0.75f, 0.9f};
constexpr float32 kWaitIn[10] = {0.374f, 0.356f, 0.565f, 0.518f, 0.551f, 0.551f, 0.656f, 0.7f, 0.759f, 0.747f};
constexpr float32 kWaitOut[10] = {0.439f, 0.517f, 0.499f, 0.477f, 0.714f, 0.794f, 0.815f, 0.926f, 0.979f, 1.283f};

int32 roundLevel(float32 v)
{
    return static_cast<int32>(std::lround(v));
}

} // namespace

int32 PlayerAudio::breathStep(float32 stamina) noexcept
{
    for (int32 i = 0; i < 10; ++i)
        if (stamina <= kSteps[i])
            return i;
    return 9; // "bad stamina mapping"
}

std::vector<PlayerAudio::Sound> PlayerAudio::update(const State& s)
{
    std::vector<Sound> out;
    if (s.dead)
        return out;
    const float32 now = s.timeMs / 1000.0f;

    // 0x45fe00: the heartbeat.
    const float32 pace = s.stamina < 0.7f ? 0.7f : s.stamina;
    if (s.health < 0.1f || s.lockedOn)
        boostPending_ = true;
    if (lastBeat_ + (secondBeat_ ? 0.72f : 0.27f) * pace < now)
    {
        secondBeat_ = !secondBeat_;
        lastBeat_ = now;
        // 0x44dab0.
        const SampleLevel l = sampleLevel(kHeart);
        Sound beat{kHeart, roundLevel(static_cast<float32>(l.level) * (2.0f - s.health)), l.range, 1.5f, 1.0f};
        if (boostPending_)
        {
            boost_ = 100.0f;
            boostPending_ = false;
        }
        if (boost_ > 0.0f)
        {
            beat.range *= std::max(1.0f, boost_ / 100.0f * 3.0f);
            beat.level = std::min(127, beat.level + roundLevel(boost_));
            boost_ -= 3.0f;
        }
        out.push_back(beat);
    }

    // 0x45ffb0: the breath.
    if (s.stamina <= 0.9f && nextBreath_ < now)
    {
        const int32 step = breathStep(s.stamina);
        const int32 sample = (breathOut_ ? kBreatheOut : kBreatheIn) + step / 2;
        nextBreath_ = now + (breathOut_ ? kWaitOut[step] : kWaitIn[step]);
        breathOut_ = !breathOut_;
        // 0x44d4d0: its rate less up to 1/256 (0x453080), then x 0.8 (0x6b3394).
        const SampleLevel l = sampleLevel(sample);
        const float32 jitter = static_cast<float32>(std::rand() % 33 - 16) / 4096.0f;
        out.push_back({sample, roundLevel(static_cast<float32>(l.level) * (1.0f - s.stamina)), l.range, 1.8f, (1.0f - jitter) * 0.8f});
    }
    return out;
}

} // namespace Game
