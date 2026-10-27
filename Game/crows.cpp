// Game/crows.cpp - the crows. See crows.h.
#include "Game/crows.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kTick = 1.0f / 30.0f;
constexpr float32 kAngleStep = 0.02453125f;  // 0x7c7a64
constexpr float32 kLookSpread = 0.00390625f; // 0x7c7a68
constexpr float32 kRing = 27.0f;             // 0x7c7a6c
constexpr float32 kAbove = 8.0f;             // 0x7c7b24 (and how far ahead of the camera they head)
constexpr float32 kSkyRay = 100.0f;          // 0x7c7b28
constexpr float32 kBodyFar = 30.0f;          // 0x7c7b2c (also the take-off line)
constexpr float32 kBodyNear = 22.0f;         // 0x7c7a74
constexpr float32 kGone2 = 900.0f;           // 0x7c7a80
constexpr float32 kStartle = 5.0f;           // 0x7c7af8
constexpr float32 kCruise = 3.0f;            // 0x7c7a90
constexpr float32 kFlyOff = 10.0f;           // 0x7c7a94
constexpr float32 kFlyAt = 20.0f;            // 0x7c7aac
constexpr float32 kEase = 0.5f;              // 0x7c7b3c
constexpr float32 kTwoPi = 6.2831853f;

float32 unit2047(std::mt19937& rng)
{
    return static_cast<float32>(std::uniform_int_distribution<int32>(0, 0x7ff)(rng)) / 2047.0f;
}

int32 byte(std::mt19937& rng)
{
    return std::uniform_int_distribution<int32>(0, 255)(rng);
}

} // namespace

float32 distanceToSegment(const Math::Vector3& p, const Math::Vector3& a, const Math::Vector3& b)
{
    const Math::Vector3 ab = b - a;
    const float32 len2 = ab.dot(ab);
    const float32 t = len2 > 1e-8f ? std::clamp((p - a).dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
    return (p - (a + ab * t)).length();
}

void Crows::update(float32 dt, const World& w, std::mt19937& rng)
{
    carry_ += dt;
    while (carry_ >= kTick)
    {
        carry_ -= kTick;
        tick(kTick, w, rng);
    }
}

void Crows::tick(float32 dt, const World& w, std::mt19937& rng)
{
    ++frame_;
    if (w.openSky && count_ < kMax)
    {
        if ((frame_ & 255u) == 6u)
        {
            // One flies in from 27 m, heading for the point 8 m ahead of the camera.
            int32 slot = 0;
            while (slot < kMax && crows_[static_cast<std::size_t>(slot)].active)
                ++slot;
            if (slot < kMax)
            {
                const float32 lookYaw = std::atan2(w.cameraLook.x, w.cameraLook.z);
                const float32 angle = (byte(rng) & 1) == 0 ? lookYaw + static_cast<float32>(byte(rng) - 128) * kLookSpread
                                                           : static_cast<float32>(byte(rng)) * kAngleStep;
                Math::Vector3 at{std::sin(angle) * kRing + w.camera.x, w.camera.y + unit2047(rng) * kAbove,
                                 std::cos(angle) * kRing + w.camera.z};
                Math::Vector3 flat{w.cameraLook.x, 0.0f, w.cameraLook.z};
                flat = flat.length() > 1e-5f ? flat.normalized() : Math::Vector3{0.0f, 0.0f, 1.0f};
                const Math::Vector3 ahead{w.camera.x + flat.x * kAbove, at.y, w.camera.z + flat.z * kAbove};
                Math::Vector3 dir = ahead - at;
                dir = dir.length() > 1e-5f ? dir.normalized() : Math::Vector3{1.0f, 0.0f, 0.0f};
                if (w.clear(at, at + dir * kSkyRay))
                {
                    Crow& c = crows_[static_cast<std::size_t>(slot)];
                    c = Crow{};
                    c.active = true;
                    c.state = 1;
                    c.pos = at;
                    c.vel = c.want = dir * kCruise;
                    c.yaw = std::atan2(dir.x, dir.z);
                    c.beatMs = 0x400;
                    ++count_;
                }
            }
        }
        else if ((frame_ & 127u) == 0x65u)
        {
            // Crows on a body 22..30 m away with the sky open above it.
            for (const Math::Vector3& body : w.bodies)
            {
                const float32 d = (body - w.camera).length();
                if (d <= kBodyNear || d >= kBodyFar || !w.clear(body, body + Math::Vector3{0.0f, kSkyRay, 0.0f}))
                    continue;
                for (Crow& c : crows_)
                    if (!c.active && count_ < kMax)
                    {
                        c = Crow{};
                        c.active = true;
                        c.state = 0;
                        c.pos = body + Math::Vector3{unit2047(rng) * 0.8f - 0.4f, 0.2f, unit2047(rng) * 0.8f - 0.4f};
                        c.yaw = unit2047(rng) * 6.28f;
                        c.beatMs = 0x200;
                        ++count_;
                    }
            }
        }
    }

    // One looked over: a twitch; gone when far, or behind the camera once flying at it.
    {
        Crow& c = crows_[frame_ % kMax];
        if (c.active)
        {
            if ((byte(rng) & 15) == 3)
            {
                c.twitchAt = w.nowMs;
                c.twitched = true;
            }
            const float32 dx = c.pos.x - w.camera.x, dz = c.pos.z - w.camera.z;
            const bool behind = c.state == 2 && (c.pos - w.camera).dot(w.cameraLook) < 0.0f;
            if (dx * dx + dz * dz > kGone2 || behind)
            {
                c = Crow{};
                --count_;
            }
        }
    }

    for (Crow& c : crows_)
    {
        if (!c.active)
            continue;
        switch (c.state)
        {
        case 0:
        {
            const float32 dx = w.player.x - c.pos.x, dz = w.player.z - c.pos.z;
            if (std::sqrt(dx * dx + dz * dz) < kStartle || w.lightning)
            {
                if ((byte(rng) & 1) == 0)
                {
                    // 0x5d0b40: up and away along a free line.
                    Math::Vector3 dir{0.0f, 1.0f, 0.0f};
                    for (int32 tries = 0; tries <= 10; ++tries)
                    {
                        const float32 a = unit2047(rng) * 6.28f;
                        dir = Math::Vector3{std::sin(a), unit2047(rng) * 0.4f + 0.2f, std::cos(a)}.normalized();
                        if (w.clear(c.pos, c.pos + dir * kBodyFar))
                            break;
                    }
                    c.want = dir * kFlyOff;
                    c.state = 1;
                }
                else
                    c.state = 2; // 0x5d0f60: at the camera
                c.vel = {static_cast<float32>(byte(rng) - 128) * 0.03f, 1.0f, static_cast<float32>(byte(rng) - 128) * 0.03f};
                c.goAt = w.nowMs + static_cast<uint32>(std::uniform_int_distribution<int32>(0, 0x1ff)(rng));
            }
            break;
        }
        case 1:
        case 2:
            if (c.goAt < w.nowMs)
            {
                float32 k = std::min(1.0f, dt * kEase);
                if (c.state == 2)
                {
                    Math::Vector3 to = w.camera - c.pos;
                    c.want = to.length() > 1e-5f ? to.normalized() * kFlyAt : c.want;
                    k = std::min(1.0f, dt);
                }
                c.vel = c.vel * (1.0f - k) + c.want * k;
                c.pos = c.pos + c.vel * dt;
                c.yaw = std::atan2(c.want.x, c.want.z);
            }
            break;
        default:
            break;
        }
    }
}

std::vector<Math::Vector3> Crows::shoot(const Math::Vector3& a, const Math::Vector3& b)
{
    std::vector<Math::Vector3> killed;
    for (Crow& c : crows_)
        if (c.active && distanceToSegment(c.pos, a, b) < 0.5f)
        {
            killed.push_back(c.pos);
            c = Crow{};
            --count_;
        }
    return killed;
}

float32 Crows::bob(int32 slot, uint32 nowMs) const
{
    const Crow& c = crows_[static_cast<std::size_t>(slot)];
    if (c.state != 1 && c.state != 2)
        return 0.0f;
    const uint32 p = c.beatMs;
    return std::sin(kTwoPi / static_cast<float32>(p) * static_cast<float32>((static_cast<uint32>(slot) * 100u + nowMs) & (p - 1u))) *
           0.1f;
}

void Crows::wing(int32 slot, uint32 nowMs, float32& elbowX, float32& elbowY, float32& tipX, float32& tipY) const
{
    elbowX = elbowY = tipX = tipY = 0.0f;
    const Crow& c = crows_[static_cast<std::size_t>(slot)];
    if (c.state != 1 && c.state != 2)
        return;
    const uint32 p = c.beatMs;
    const uint32 phase = static_cast<uint32>(slot) * 100u + nowMs;
    // Unfolding over the first 400 ms of the flight.
    const uint32 since = nowMs > c.goAt ? nowMs - c.goAt : 0u;
    const float32 half = since < 400u ? static_cast<float32>(since) / 400.0f * kEase : kEase;
    const float32 inner = -std::sin(kTwoPi / static_cast<float32>(p) * static_cast<float32>((phase + p / 6u) & (p - 1u)));
    const float32 outer = -std::sin(kTwoPi / static_cast<float32>(p) * static_cast<float32>(phase & (p - 1u)));
    elbowX = half * std::cos(inner);
    elbowY = half * std::sin(inner);
    tipX = elbowX + half * std::cos(outer);
    tipY = elbowY + half * std::sin(outer);
}

} // namespace Game
