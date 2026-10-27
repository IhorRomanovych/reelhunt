// Game/crane.cpp - a pad-turned entity. See crane.h.
#include "Game/crane.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kTick = 1.0f / 30.0f;
constexpr float32 kStep = 0.05f;          // 0x739210
constexpr float32 kMaxRate = 2.0f;        // 0x739208
constexpr float32 kDegreesPerRate = -18.0f; // 0x69b4b0 (a second)

} // namespace

void Hanging::update(float32 dt, const Math::Vector3& pivot, float32 length, float32 spin)
{
    length = std::max(length, 0.1f);
    if (!started_)
    {
        bob_ = pivot + Math::Vector3{0.0f, -length, 0.0f};
        started_ = true;
    }
    carry_ += dt;
    while (carry_ >= kTick)
    {
        carry_ -= kTick;
        vel_.y -= 25.0f * kTick; // 0x724890
        bob_ += vel_ * kTick;
        Math::Vector3 d = bob_ - pivot;
        const float32 l = d.length();
        d = l > 1e-5f ? d * (1.0f / l) : Math::Vector3{0.0f, -1.0f, 0.0f};
        bob_ = pivot + d * length;
        vel_ -= d * vel_.dot(d); // along the rope it does not move
        vel_ *= 1.0f - std::min(spin, 1.0f);
        down_ = d;
    }
}

float32 CraneTurn::update(float32 dt, bool left, bool right)
{
    float32 turned = 0.0f;
    carry_ += dt;
    while (carry_ >= kTick)
    {
        carry_ -= kTick;
        if (left)
            rate_ -= kStep;
        else if (right)
            rate_ += kStep;
        else if (rate_ < 0.0f)
            rate_ += std::min(kStep, -rate_);
        else
            rate_ -= std::min(kStep, rate_);
        rate_ = std::clamp(rate_, -kMaxRate, kMaxRate);
        turned += rate_ * kDegreesPerRate * kTick * (3.14159265f / 180.0f);
    }
    return turned;
}

} // namespace Game
