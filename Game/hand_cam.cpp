// Game/hand_cam.cpp - the hand-held camera shake. See hand_cam.h.
#include "Game/hand_cam.h"

#include <cmath>

namespace Game {

namespace {

constexpr float32 kTick = 1.0f / 30.0f;
constexpr float32 kDegrees = 57.29578f;

float32 wrap(float32 deg)
{
    while (deg > 180.0f)
        deg -= 360.0f;
    while (deg < -180.0f)
        deg += 360.0f;
    return deg;
}

// The pull on a rate from the sign of the error (0x591840).
void pull(float32& rate, float32 err, const HandCam::Set& s)
{
    const float32 f = (rate < 0.0f) == (err < 0.0f) ? 1.0f : s.braking;
    if (err < 0.0f)
        rate -= s.spring * f;
    else if (err > 0.0f)
        rate += s.spring * f;
}

int32 rand15(std::mt19937& rng)
{
    return std::uniform_int_distribution<int32>(0, 0x7fff)(rng);
}

bool kickNow(const HandCam::Set& s, std::mt19937& rng)
{
    return s.period != 0 && rand15(rng) % s.period == 1;
}

float32 kickBy(const HandCam::Set& s, std::mt19937& rng)
{
    const int32 a = rand15(rng) % 2000;
    return static_cast<float32>(rand15(rng) % 2000 - a) * s.kick;
}

} // namespace

void HandCam::update(float32 dt, float32 yaw, float32 pitch, const Set& look, const Set& roll, std::mt19937& rng)
{
    carry_ += dt;
    while (carry_ >= kTick)
    {
        carry_ -= kTick;
        tick(yaw * kDegrees, pitch * kDegrees, look, roll, rng);
    }
}

void HandCam::tick(float32 yaw, float32 pitch, const Set& look, const Set& roll, std::mt19937& rng)
{
    if (!started_)
    {
        lastYaw_ = yaw;
        lastPitch_ = pitch;
        started_ = true;
    }
    // Where the real view points from last frame's shaken one.
    pull(yawRate_, wrap(yaw - (lastYaw_ + yaw_)), look);
    yaw_ += yawRate_;
    pull(pitchRate_, wrap(pitch - (lastPitch_ + pitch_)), look);
    pull(rollRate_, wrap(-roll_), roll);
    if (kickNow(look, rng))
    {
        pitchRate_ += kickBy(look, rng);
        yawRate_ += kickBy(look, rng);
    }
    if (kickNow(roll, rng))
        rollRate_ += kickBy(roll, rng);
    roll_ += rollRate_;
    pitch_ += pitchRate_;
    lastYaw_ = yaw;
    lastPitch_ = pitch;
}

float32 HandCam::yaw() const noexcept
{
    return yaw_ / kDegrees;
}

float32 HandCam::pitch() const noexcept
{
    return pitch_ / kDegrees;
}

float32 HandCam::roll() const noexcept
{
    return roll_ / kDegrees;
}

} // namespace Game
