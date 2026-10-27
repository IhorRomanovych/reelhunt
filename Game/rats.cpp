// Game/rats.cpp - the rats. See rats.h.
#include "Game/rats.h"

#include "Game/crows.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kTick = 1.0f / 30.0f;
constexpr float32 kAngleStep = 0.02453125f;  // 0x7c7a64: 2 pi / 256
constexpr float32 kLookSpread = 0.00390625f; // 0x7c7a68: 1 / 256 rad per step
constexpr float32 kRayFrom = 2.0f;           // 0x7c7a70
constexpr float32 kRayDepth = 22.0f;         // 0x7c7a74
constexpr float32 kFloorReach = 7.0f;        // 0x7c7a78
constexpr float32 kSpawnDark = 1.5f;         // 0x7c7a7c
constexpr float32 kGone2 = 900.0f;           // 0x7c7a80: 30 m
constexpr float32 kFlee = 3.0f;              // 0x7c7a90
constexpr float32 kDrawn = 10.0f;            // 0x7c7a94
constexpr float32 kNear = 1.0f;              // 0x7c7a98
constexpr float32 kStride = 2.3f;            // 0x7c7a9c
constexpr float32 kMostOfTheWay = 0.75f;     // 0x7c7aa0
constexpr float32 kWander = 0.03f;           // 0x7c7aa4
constexpr float32 kWanderBias = 0.01f;       // 0x7c7aa8
constexpr float32 kPack = 20.0f;             // 0x7c7aac
constexpr float32 kWalkMs = 2000.0f;         // 0x7c7ab0
constexpr float32 kRunMs = 600.0f;           // 0x7c7ab4
constexpr float32 kTurn = 0.4f;              // 0x7c7ac0
constexpr float32 kSquash = 0.35f;           // 0x7c7ac4
constexpr float32 kMoveDark = 1.7f;          // 0x7c7acc
constexpr float32 kLookUp = 0.3f;            // 0x7c7ad0
constexpr float32 kLookBeyond = 0.2f;        // 0x7c7ad4
constexpr float32 kBodyReach = 25.0f;
constexpr float32 kPi = 3.14159265f;

int32 byte(std::mt19937& rng)
{
    return std::uniform_int_distribution<int32>(0, 255)(rng);
}

float32 flat(const Math::Vector3& a, const Math::Vector3& b)
{
    const float32 dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

} // namespace

float32 Rats::scale(int32 slot) noexcept
{
    static constexpr float32 kScales[5] = {1.0f, 0.8f, 1.1f, 1.07f, 0.7f}; // 0x7c7ad8
    return kScales[slot % 5];
}

float32 Rats::drawYaw(int32 slot, uint32 nowMs) const noexcept
{
    const Rat& r = rats_[static_cast<std::size_t>(slot)];
    if (r.state == 1 || r.state == 2)
        return r.yaw + ((nowMs & 0x80u) ? 0.1f : -0.1f);
    return r.yaw;
}

void Rats::clear()
{
    rats_ = {};
    count_ = 0;
}

std::vector<Math::Vector3> Rats::update(float32 dt, const World& w, std::mt19937& rng)
{
    std::vector<Math::Vector3> squashed;
    carry_ += dt;
    while (carry_ >= kTick)
    {
        carry_ -= kTick;
        tick(w, rng, squashed);
    }
    // Moving between ticks (0x5cdd10's lerp runs on the clock).
    for (Rat& r : rats_)
        if (r.active && (r.state == 1 || r.state == 2))
        {
            const float32 t = static_cast<float32>(w.nowMs - r.moveStart) / (r.state == 1 ? kWalkMs : kRunMs);
            if (t < 1.0f)
                r.pos = r.from + (r.to - r.from) * t;
        }
    return squashed;
}

std::vector<Math::Vector3> Rats::shoot(const Math::Vector3& a, const Math::Vector3& b)
{
    std::vector<Math::Vector3> killed;
    for (Rat& r : rats_)
        if (r.active && distanceToSegment(r.pos, a, b) < 0.5f)
        {
            killed.push_back(r.pos);
            r = Rat{};
            --count_;
        }
    return killed;
}

bool Rats::move(Rat& r, const Math::Vector3& target, uint8 mode, const World& w) const
{
    if (target.x == r.from.x && target.y == r.from.y && target.z == r.from.z)
        return false;
    const auto floor = w.floor(target.x, target.y + kNear, target.z, kRayDepth);
    if (!floor || std::fabs(r.pos.y - floor->y) >= kNear)
        return false;
    // Walking, it keeps to the dark (or to no brighter than here).
    if (mode != 2 && floor->light >= kMoveDark && r.light < floor->light)
        return false;
    const Math::Vector3 to{target.x, floor->y, target.z};
    const Math::Vector3 a = r.pos + Math::Vector3{0.0f, kLookUp, 0.0f};
    Math::Vector3 b = to + Math::Vector3{0.0f, kLookUp, 0.0f};
    const float32 len = (b - a).length();
    if (len > 1e-4f)
        b = a + (b - a) * ((len + kLookBeyond) / len);
    if (!w.clear(a, b))
        return false;
    r.from = r.pos;
    r.to = to;
    r.light = floor->light;
    r.colour = floor->colour;
    r.state = mode;
    r.moveStart = w.nowMs;
    r.wantYaw = std::atan2(to.x - r.from.x, to.z - r.from.z);
    return true;
}

void Rats::tick(const World& w, std::mt19937& rng, std::vector<Math::Vector3>& squashed)
{
    ++frame_;
    // What draws them: the nearest body, or the dead player.
    if ((frame_ & 63u) == 54u)
    {
        attract_ = false;
        float32 best = kBodyReach;
        for (const Math::Vector3& b : w.bodies)
            if (const float32 d = (b - w.camera).length(); d < best)
            {
                best = d;
                attract_ = true;
                attractAt_ = b;
            }
    }
    if (!w.playerAlive)
    {
        attract_ = true;
        attractAt_ = w.player;
    }

    // One appears.
    if (count_ < limit_ && (frame_ & 15u) == 6u)
    {
        int32 slot = 0;
        while (slot < kMax && rats_[static_cast<std::size_t>(slot)].active)
            ++slot;
        if (slot < kMax)
        {
            const float32 angle = (byte(rng) & 1) == 0 ? w.cameraYaw + static_cast<float32>(byte(rng) - 128) * kLookSpread
                                                       : static_cast<float32>(byte(rng)) * kAngleStep;
            const float32 x = std::sin(angle) * w.spawnRange + w.camera.x, z = std::cos(angle) * w.spawnRange + w.camera.z;
            const auto floor = w.floor(x, w.camera.y + kRayFrom, z, kRayDepth);
            if (floor && std::fabs(floor->y - w.camera.y) < kFloorReach && floor->light < kSpawnDark)
            {
                Rat& r = rats_[static_cast<std::size_t>(slot)];
                r = Rat{};
                r.active = true;
                r.pos = r.from = r.to = {x, floor->y, z};
                r.light = floor->light;
                r.colour = floor->colour;
                r.yaw = r.wantYaw = static_cast<float32>(byte(rng)) * kAngleStep;
                ++count_;
            }
        }
    }

    // One is looked over: it may twitch, and it goes when far from the camera.
    {
        Rat& r = rats_[frame_ & 15u];
        if (r.active)
        {
            if ((byte(rng) & 15) == 3)
            {
                r.twitchAt = w.nowMs;
                r.twitched = true;
            }
            const float32 d = flat(r.pos, w.camera);
            if (d * d > kGone2)
            {
                r = Rat{};
                --count_;
            }
        }
    }

    for (int32 i = 0; i < kMax; ++i)
    {
        Rat& r = rats_[static_cast<std::size_t>(i)];
        if (!r.active)
            continue;
        const float32 toPlayer = flat(r.pos, w.player);
        switch (r.state)
        {
        case 0:
            think(i, w, rng);
            break;
        case 1:
        case 2:
            if (static_cast<float32>(w.nowMs - r.moveStart) >= (r.state == 1 ? kWalkMs : kRunMs))
            {
                r.state = 0;
                r.pos = r.to;
            }
            break;
        default:
            break;
        }
        // It turns towards where it goes.
        float32 d = r.wantYaw - r.yaw;
        while (d < -kPi)
            d += 2.0f * kPi;
        while (d > kPi)
            d -= 2.0f * kPi;
        r.yaw = std::fabs(d) < kTurn ? r.wantYaw : r.yaw + (d < 0.0f ? -kTurn : kTurn);
        // Under foot.
        if (toPlayer < kSquash && w.playerAlive)
        {
            squashed.push_back(r.pos);
            r = Rat{};
            --count_;
        }
    }
}

void Rats::think(int32 slot, const World& w, std::mt19937& rng)
{
    Rat& r = rats_[static_cast<std::size_t>(slot)];
    const float32 toPlayer = flat(r.pos, w.player);
    if (toPlayer < kFlee && w.playerAlive)
    {
        // Away from him, else to one side, else the other.
        const float32 k = kFlee / std::max(toPlayer, 1e-3f);
        const float32 dx = (w.player.x - r.pos.x) * k, dz = (w.player.z - r.pos.z) * k;
        if (!move(r, {r.pos.x - dx, r.pos.y, r.pos.z - dz}, 2, w) && !move(r, {r.pos.x + dz, r.pos.y, r.pos.z - dx}, 2, w))
            move(r, {r.pos.x - dz, r.pos.y, r.pos.z + dx}, 2, w);
        return;
    }
    if ((byte(rng) & 63) != 18 && !w.lightning)
        return;
    auto towards = [&](const Math::Vector3& at, float32 d) {
        const float32 f = std::min(kStride / d, kMostOfTheWay);
        return Math::Vector3{r.pos.x + (at.x - r.pos.x) * f, r.pos.y, r.pos.z + (at.z - r.pos.z) * f};
    };
    if (attract_)
    {
        const float32 d = flat(r.pos, attractAt_);
        if (d < kDrawn)
        {
            if (d > kNear)
                move(r, towards(attractAt_, d), 2, w);
            return;
        }
    }
    if ((byte(rng) & 8) == 0)
    {
        // Towards the nearest other rat.
        float32 best = 1000.0f;
        const Rat* other = nullptr;
        for (int32 j = 0; j < kMax; ++j)
            if (j != slot && rats_[static_cast<std::size_t>(j)].active)
                if (const float32 d = flat(r.pos, rats_[static_cast<std::size_t>(j)].pos); d < best)
                {
                    best = d;
                    other = &rats_[static_cast<std::size_t>(j)];
                }
        if (other && best < kPack && best > kNear)
        {
            const Math::Vector3 target = towards(other->pos, best);
            if (flat(target, w.player) > kFlee)
                move(r, target, 2, w);
        }
    }
    else
    {
        const Math::Vector3 target{r.pos.x + static_cast<float32>(byte(rng) - 128) * kWander + kWanderBias, r.pos.y,
                                   r.pos.z + static_cast<float32>(byte(rng) - 128) * kWander};
        if (flat(target, w.player) > kFlee)
            move(r, target, 1, w);
    }
}

} // namespace Game
