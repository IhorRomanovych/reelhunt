// Game/acoustic_probe.cpp - see acoustic_probe.h.
#include "Game/acoustic_probe.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kHead = 1.8f;    // 0x6b29b0
constexpr float32 kUp = 30.0f;     // 0x6b29e8
constexpr float32 kNear = 5.0f;    // 0x6b29b4
constexpr float32 kFar = 10.0f;    // 0x6b29bc
constexpr float32 kStep = 18.0f;   // 0x6b29c4 (the matrix turns by -18, 0x6b29fc)

} // namespace

float32 AcousticProbe::weight(int32 ray, int32 side) noexcept
{
    const float32 base = side < 0 ? 90.0f : 270.0f; // 0x6b2a0c / 0x6b2a18
    return std::fabs(std::fmod(static_cast<float32>(ray) * kStep + base, 360.0f) / 180.0f - 1.0f);
}

void AcousticProbe::update(const Math::Vector3& eye, const Math::Vector3& forward, const Math::Vector3& playerFeet, const Ray& ray)
{
    const uint32 c = frame_ % 10u;
    ++frame_;
    if (c == 0)
    {
        Math::Vector3 f{forward.x, 0.0f, forward.z};
        const float32 len = f.length();
        forward_ = len > 1e-4f ? f * (1.0f / len) : Math::Vector3{0.0f, 0.0f, -1.0f};
        listener_ = eye;
        const Math::Vector3 head{eye.x, playerFeet.y + kHead, eye.z};
        if (head.y != eye.y && !ray(eye, head))
            listener_ = head;
        ceiling_ = ray(listener_, listener_ + Math::Vector3{0.0f, kUp, 0.0f}).value_or(0.0f);
    }
    // The frame's ray and the one opposite, clockwise from the matrix's facing seen from above.
    const Math::Vector3 right{-forward_.z, 0.0f, forward_.x};
    for (const uint32 i : {c, c + 10u})
    {
        const float32 a = static_cast<float32>(i) * kStep * 3.14159265f / 180.0f;
        const Math::Vector3 dir = forward_ * std::cos(a) + right * std::sin(a);
        Sample& s = rays[i];
        s.wall = ray(listener_, listener_ + dir * kFar).value_or(0.0f);
        const Math::Vector3 p5 = listener_ + dir * kNear, p10 = listener_ + dir * kFar;
        s.roof5 = ray(p5, p5 + Math::Vector3{0.0f, kUp, 0.0f}).has_value();
        s.roof10 = ray(p10, p10 + Math::Vector3{0.0f, kUp, 0.0f}).has_value();
    }
}

float32 AcousticProbe::shut(int32 side) const noexcept
{
    if (ceiling_ == 0.0f)
        return 0.0f;
    float32 s = 1.0f;
    for (int32 i = 0; i < kRays; ++i)
    {
        const Sample& r = rays[static_cast<std::size_t>(i)];
        const bool open = ((r.wall > kNear || r.wall == 0.0f) && !r.roof5) || (r.wall == 0.0f && !r.roof10);
        if (open)
            s -= (weight(i, side) * 0.8f + 0.1f) * 3.0f / kFar;
    }
    return std::max(0.0f, s);
}

float32 AcousticProbe::close(int32 side) const noexcept
{
    float32 c = 0.0f;
    for (int32 i = 0; i < kRays; ++i)
    {
        const float32 wall = rays[static_cast<std::size_t>(i)].wall;
        if (wall > 0.0f)
            c += std::max(0.0f, (kNear - wall) / kNear) * (weight(i, side) * 0.8f + 0.1f) / kFar;
    }
    return std::min(1.0f, c);
}

float32 AcousticProbe::tail(int32 side) const noexcept
{
    return (1.0f - close(side) / 1.25f) * (1.0f - shut(side)); // 0x6b2a20
}

} // namespace Game
