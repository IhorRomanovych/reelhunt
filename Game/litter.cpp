// Game/litter.cpp - the litter. See litter.h.
#include "Game/litter.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kTick = 1.0f / 30.0f;
constexpr float32 kGone2 = 400.0f;      // 0x7c53dc
constexpr float32 kLayNear = 0.0784313753f; // 0x7c53e0 (x rand & 255: 0..20 m)
constexpr float32 kLayStep = 0.00392156886f; // 0x7c53e4
constexpr float32 kLayFar = 19.0f;      // 0x7c53e8 (also where the drawing ends)
constexpr float32 kAngleStep = 0.0245312508f; // 0x7c53ec
constexpr float32 kLookSpread = 0.00390625f;  // 0x7c53f0
constexpr float32 kLayDepth = 20.0f;    // 0x7c53f4
constexpr float32 kUpright = 0.5f;      // 0x7c53fc
constexpr float32 kAbove = 0.1f;        // 0x7c5400
constexpr float32 kSpin = 2.0f;         // 0x7c540c (rad a second)
constexpr float32 kTwoPi = 6.28f;       // 0x7c5408
constexpr float32 kPi = 3.14f;          // 0x7c5404

// GTA3's aAnimations (0x7c5238): by type, 17 horizontal fractions then 17 heights.
constexpr float32 kAnimations[3][34] = {
    {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 0.05f, 0.12f, 0.25f, 0.42f, 0.57f, 0.68f, 0.8f, 0.86f, 0.9f, 0.93f, 0.95f, 0.96f, 0.97f, 0.98f, 0.99f, 1.0f,
     0.0f, 0.2f, 0.5f, 0.9f, 1.2f, 1.25f, 1.3f, 1.2f, 1.1f, 0.95f, 0.8f, 0.6f, 0.45f, 0.3f, 0.2f, 0.1f, 0.0f},
    {0.0f, 0.05f, 0.12f, 0.25f, 0.42f, 0.57f, 0.68f, 0.8f, 0.95f, 1.1f, 1.15f, 1.18f, 1.15f, 1.1f, 1.05f, 1.03f, 1.0f,
     0.0f, 0.2f, 0.5f, 0.9f, 1.2f, 1.25f, 1.3f, 1.2f, 1.1f, 0.95f, 0.8f, 0.6f, 0.45f, 0.3f, 0.2f, 0.1f, 0.0f},
};

int32 rand15(std::mt19937& rng)
{
    return std::uniform_int_distribution<int32>(0, 0x7fff)(rng);
}

float32 unit2047(std::mt19937& rng)
{
    return static_cast<float32>(rand15(rng) & 0x7ff) / 2047.0f;
}

float32 flat(const Math::Vector3& a, const Math::Vector3& b)
{
    const float32 dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

void Litter::update(float32 dt, const World& w, std::mt19937& rng)
{
    carry_ += dt;
    while (carry_ >= kTick)
    {
        carry_ -= kTick;
        tick(kTick, w, rng);
    }
}

void Litter::tick(float32 dt, const World& w, std::mt19937& rng)
{
    ++frame_;
    alpha_ = on_ ? std::min(255, alpha_ + 5) : std::max(0, alpha_ - 5);
    if (alpha_ == 0)
        return;
    // The scripted camera has let go: fill round the camera at once.
    const bool refill = !w.scripted && wasScripted_;
    wasScripted_ = w.scripted;

    // Gone beyond 20 m: two sheets a frame, or all of them on a refill.
    const int32 first = refill ? 0 : static_cast<int32>(frame_ & 15u) * 2;
    const int32 last = refill ? kSheets : first + 2;
    for (int32 i = first; i < last; ++i)
    {
        Sheet& s = sheets_[static_cast<std::size_t>(i)];
        if (s.state == 1)
        {
            const float32 dx = s.base.x - w.camera.x, dz = s.base.z - w.camera.z;
            if (dx * dx + dz * dz > kGone2)
                s = Sheet{};
        }
    }

    // Laid down round the camera.
    const int32 tries = refill ? 30 : 1;
    for (int32 n = 0; n < tries && w.hit; ++n)
    {
        auto freeSheet = std::find_if(sheets_.begin(), sheets_.end(), [](const Sheet& s) { return s.state == 0; });
        if (freeSheet == sheets_.end())
            break;
        const float32 dist = refill ? static_cast<float32>(rand15(rng) & 0xff) * kLayNear
                                    : static_cast<float32>(rand15(rng) & 0xff) * kLayStep + kLayFar;
        float32 angle;
        if ((rand15(rng) & 1) == 0)
        {
            const float32 lx = w.cameraLook.x == 0.0f ? 0.01f : w.cameraLook.x;
            angle = std::atan2(lx, w.cameraLook.z) + static_cast<float32>((rand15(rng) & 0xff) - 0x80) * kLookSpread;
        }
        else
            angle = static_cast<float32>(rand15(rng) & 0xff) * kAngleStep;
        const Math::Vector3 top{std::sin(angle) * dist + w.camera.x, w.camera.y, std::cos(angle) * dist + w.camera.z};
        const auto ground = w.hit(top, top - Math::Vector3{0.0f, kLayDepth, 0.0f});
        if (!ground || ground->normal.y < kUpright)
            continue;
        Sheet& s = *freeSheet;
        s = Sheet{};
        s.base = {top.x, ground->at.y + kAbove, top.z};
        s.angle = static_cast<float32>(rand15(rng) & 0xff) * kAngleStep;
        s.state = 1;
    }

    // Moving.
    for (Sheet& s : sheets_)
    {
        if (s.state != 2)
            continue;
        const uint32 t = w.nowMs - s.moveStart;
        if (t < s.moveFor)
        {
            const uint32 stepLen = std::max(1u, s.moveFor >> 4);
            const int32 step = std::min(15, static_cast<int32>(t * 16u / s.moveFor));
            const float32 frac = static_cast<float32>(t - static_cast<uint32>(step) * stepLen) / static_cast<float32>(stepLen);
            const float32 tt = static_cast<float32>(t) / static_cast<float32>(s.moveFor);
            const float32* anim = kAnimations[std::min<int32>(s.type, 2)];
            const float32 across = (1.0f - frac) * anim[step] + frac * anim[step + 1];
            const float32 up = (1.0f - frac) * anim[17 + step] + frac * anim[18 + step];
            s.moving.y = (1.0f - tt) * s.base.y + tt * s.targetY + up * s.lift;
            if (s.type == 2)
            {
                const float32 a = (0.5f - std::cos(tt * kPi) * 0.5f) * static_cast<float32>(s.turns) * kTwoPi;
                const float32 dx = s.base.x - s.whirl.x, dz = s.base.z - s.whirl.z;
                s.moving.x = dz * std::sin(a) + dx * std::cos(a) + s.whirl.x;
                s.moving.z = dz * std::cos(a) + s.whirl.z - dx * std::sin(a);
            }
            else
            {
                s.moving.x = s.base.x + s.driftX * across;
                s.moving.z = s.base.z + s.driftZ * across;
            }
            s.angle += dt * kSpin;
            if (s.angle > kTwoPi)
                s.angle -= kTwoPi;
        }
        else
        {
            s.base.x += s.driftX;
            s.base.z += s.driftZ;
            s.base.y = s.targetY;
            s.state = 1;
            s.type = 0;
        }
    }

    if (w.wind > 0.15f && (frame_ & 127u) == 0x4cu)
        whirlwind(w, rng);

    // Blown along.
    const uint32 mask = w.wind < 0.1f ? 31u : w.wind < 0.4f ? 7u : w.wind < 0.7f ? 1u : 0u;
    if ((frame_ & mask) == 0u && w.hit)
    {
        Sheet& s = sheets_[static_cast<std::size_t>(rand15(rng) & 31)];
        if (s.state == 1)
        {
            s.moveStart = w.nowMs;
            s.moveFor = static_cast<uint32>(std::floor(w.wind * 1500.0f + 1000.0f));
            s.lift = 0.2f;
            s.driftX = s.driftZ = w.wind * 3.0f;
            const Math::Vector3 top{s.base.x + s.driftX, s.base.y + 3.0f, s.base.z + s.driftZ};
            const auto ground = w.hit(top, top - Math::Vector3{0.0f, 10.0f, 0.0f});
            if (ground && ground->normal.y > kUpright)
            {
                s.targetY = ground->at.y + kAbove;
                if (std::abs(s.targetY - s.base.y) < 1.5f)
                {
                    s.state = 2;
                    s.type = 1;
                    s.moving = s.base;
                }
            }
        }
    }
}

void Litter::whirlwind(const World& w, std::mt19937& rng)
{
    if (!w.hit)
        return;
    const Math::Vector3 look = w.cameraLook.length() > 1e-5f ? w.cameraLook.normalized() : Math::Vector3{0.0f, 0.0f, 1.0f};
    Math::Vector3 c = w.camera + look * 5.0f;
    c.x += unit2047(rng) * 12.0f - 6.0f;
    c.z += unit2047(rng) * 12.0f - 6.0f;
    if (w.hit(c, c + Math::Vector3{0.0f, 100.0f, 0.0f}))
        return;
    // The clearance: four flat tests, each no longer than the last clearance.
    float32 r = 9.0f;
    for (const Math::Vector3 d : {Math::Vector3{1.0f, 0.0f, 0.0f}, Math::Vector3{-1.0f, 0.0f, 0.0f},
                                  Math::Vector3{0.0f, 0.0f, 1.0f}, Math::Vector3{0.0f, 0.0f, -1.0f}})
        if (const auto h = w.hit(c, c + d * r))
        {
            r = (h->at - c).length();
            if (r < 2.0f)
                return;
        }
    for (Sheet& s : sheets_)
    {
        if (s.state != 1)
            continue;
        const float32 d = flat(s.base, c);
        if (d >= r)
            continue;
        s.moveStart = w.nowMs;
        s.moveFor = static_cast<uint32>((rand15(rng) & 0x7ff) + 10000);
        s.lift = static_cast<float32>(rand15(rng) & 0x7ff) * 0.001f + 3.0f;
        s.driftX = s.driftZ = 0.0f;
        s.state = 2;
        s.type = 2;
        s.moving = s.base;
        s.targetY = s.base.y;
        s.whirl = {c.x, 0.0f, c.z};
        s.turns = static_cast<uint8>(std::max(1, static_cast<int32>(std::floor((1.0f - d / r) * 10.0f))));
    }
}

Math::Vector3 Litter::position(int32 i) const
{
    const Sheet& s = sheets_[static_cast<std::size_t>(i)];
    return s.state == 1 ? s.base : s.moving;
}

int32 Litter::drawAlpha(int32 i, const Math::Vector3& camera) const
{
    const Sheet& s = sheets_[static_cast<std::size_t>(i)];
    if (s.state == 0)
        return 0;
    const float32 d = flat(position(i), camera);
    if (d >= kLayFar)
        return 0;
    const int32 a = d < 16.0f ? 100 : static_cast<int32>(std::lround(100.0f - (d - 16.0f) * 100.0f / 3.0f));
    return a * alpha_ >> 8;
}

} // namespace Game
