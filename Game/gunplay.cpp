// Game/gunplay.cpp - the player's firearms. See gunplay.h.
#include "Game/gunplay.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "Assets/ini_records.h"
#include "Math/math.h"

namespace Game {

namespace {

// The part of the line origin + dir * t (t >= 0) inside the upright cylinder about `axis`
// (x, z) of radius r between heights y0 and y1: its entry t, if it enters at all.
std::optional<float32> enterCylinder(const Math::Vector3& axis, float32 r, float32 y0, float32 y1,
                                     const Math::Vector3& origin, const Math::Vector3& dir)
{
    float32 tMin = 0.0f, tMax = 1e30f;
    // Across: |(o + d t - axis).xz| <= r.
    const float32 ox = origin.x - axis.x, oz = origin.z - axis.z;
    const float32 a = dir.x * dir.x + dir.z * dir.z;
    const float32 c = ox * ox + oz * oz - r * r;
    if (a < 1e-9f)
    {
        if (c > 0.0f)
            return std::nullopt; // straight up or down, outside it
    }
    else
    {
        const float32 b = 2.0f * (ox * dir.x + oz * dir.z);
        const float32 disc = b * b - 4.0f * a * c;
        if (disc < 0.0f)
            return std::nullopt;
        const float32 s = std::sqrt(disc);
        tMin = std::max(tMin, (-b - s) / (2.0f * a));
        tMax = std::min(tMax, (-b + s) / (2.0f * a));
    }
    // Up: y0 <= o.y + d.y t <= y1.
    if (std::fabs(dir.y) < 1e-9f)
    {
        if (origin.y < y0 || origin.y > y1)
            return std::nullopt;
    }
    else
    {
        float32 t0 = (y0 - origin.y) / dir.y, t1 = (y1 - origin.y) / dir.y;
        if (t0 > t1)
            std::swap(t0, t1);
        tMin = std::max(tMin, t0);
        tMax = std::min(tMax, t1);
    }
    if (tMin > tMax)
        return std::nullopt;
    return tMin;
}

} // namespace

const char* hitZoneName(HitZone z)
{
    switch (z)
    {
    case HitZone::Head: return "head";
    case HitZone::Torso: return "torso";
    case HitZone::Arm: return "arm";
    case HitZone::Leg: return "leg";
    case HitZone::None: break;
    }
    return "none";
}

BodyHit shotHitsBody(const Math::Vector3& feet, float32 yaw, const Math::Vector3& origin, const Math::Vector3& dir,
                     float32 range)
{
    BodyHit hit;
    const auto body = enterCylinder(feet, kBodyRadius, feet.y, feet.y + kNeckHeight, origin, dir);
    const auto head = enterCylinder(feet, kHeadRadius, feet.y + kNeckHeight, feet.y + kHeadTop, origin, dir);
    std::optional<float32> t;
    bool isHead = false;
    if (body && (!head || *body <= *head))
        t = body;
    else if (head)
    {
        t = head;
        isHead = true;
    }
    if (!t || *t > range)
        return hit;
    hit.distance = *t;
    hit.point = origin + dir * *t;
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Math::Vector3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    const Math::Vector3 rel{hit.point.x - feet.x, 0.0f, hit.point.z - feet.z};
    const float32 lateral = rel.dot(right);
    hit.left = lateral < 0.0f;
    hit.fromFront = fwd.dot(Math::Vector3{origin.x - feet.x, 0.0f, origin.z - feet.z}) >= 0.0f;
    const float32 height = hit.point.y - feet.y;
    if (isHead)
        hit.zone = HitZone::Head;
    else if (height < kHipHeight)
        hit.zone = HitZone::Leg;
    else
        hit.zone = std::fabs(lateral) > kShoulderHalfWidth ? HitZone::Arm : HitZone::Torso;
    return hit;
}

std::vector<BodyPart> bodyParts(const std::unordered_map<int16, Math::Vector3>& bones)
{
    struct Segment {
        int16 a, b;
        float32 radius;
        HitZone zone;
        bool left;
    };
    // Radii ours (a broad man's limbs).
    static const Segment kSegments[] = {
        {1045, 1096, 0.17f, HitZone::Torso, false}, // pelvis to chest
        {1096, 1040, 0.15f, HitZone::Torso, false}, // chest to neck
        {1040, 1039, 0.07f, HitZone::Arm, true},    // shoulders
        {1040, 1093, 0.07f, HitZone::Arm, false},
        {1039, 1020, 0.065f, HitZone::Arm, true},   // upper arms
        {1093, 1074, 0.065f, HitZone::Arm, false},
        {1020, 1021, 0.055f, HitZone::Arm, true},   // forearms
        {1074, 1075, 0.055f, HitZone::Arm, false},
        {1021, 1006, 0.05f, HitZone::Arm, true},    // hands
        {1075, 1060, 0.05f, HitZone::Arm, false},
        {1023, 1002, 0.09f, HitZone::Leg, true},    // thighs
        {1077, 1056, 0.09f, HitZone::Leg, false},
        {1002, 1019, 0.07f, HitZone::Leg, true},    // calves
        {1056, 1073, 0.07f, HitZone::Leg, false},
        {1019, 1024, 0.05f, HitZone::Leg, true},    // feet
        {1073, 1078, 0.05f, HitZone::Leg, false},
    };
    std::vector<BodyPart> parts;
    auto at = [&](int16 id, Math::Vector3& out) {
        const auto it = bones.find(id);
        if (it == bones.end())
            return false;
        out = it->second;
        return true;
    };
    for (const Segment& s : kSegments)
    {
        BodyPart p;
        if (!at(s.a, p.a) || !at(s.b, p.b))
            return {};
        p.radius = s.radius;
        p.zone = s.zone;
        p.left = s.left;
        parts.push_back(p);
    }
    // The head: from the neck through the head bone on to the crown (0.15 m on: ours).
    BodyPart head;
    if (!at(1040, head.a) || !at(1001, head.b))
        return {};
    head.b = head.b + (head.b - head.a).normalized() * 0.15f;
    head.radius = 0.12f;
    head.zone = HitZone::Head;
    parts.push_back(head);
    return parts;
}

namespace {

float32 distanceToSegment(const Math::Vector3& p, const Math::Vector3& a, const Math::Vector3& b)
{
    const Math::Vector3 ab = b - a;
    const float32 len2 = ab.dot(ab);
    const float32 t = len2 > 1e-12f ? std::clamp((p - a).dot(ab) / len2, 0.0f, 1.0f) : 0.0f;
    return (p - (a + ab * t)).length();
}

// Where the line origin + dir * t (0 <= t <= range) enters the capsule, if it does. The distance
// to the capsule's axis is convex along the line: its lowest point is found by ternary search,
// then the entry by bisection before it.
std::optional<float32> enterCapsule(const BodyPart& c, const Math::Vector3& origin, const Math::Vector3& dir, float32 range)
{
    auto gap = [&](float32 t) { return distanceToSegment(origin + dir * t, c.a, c.b) - c.radius; };
    if (gap(0.0f) <= 0.0f)
        return 0.0f;
    float32 lo = 0.0f, hi = range;
    for (int i = 0; i < 60; ++i)
    {
        const float32 m1 = lo + (hi - lo) / 3.0f, m2 = hi - (hi - lo) / 3.0f;
        if (gap(m1) < gap(m2))
            hi = m2;
        else
            lo = m1;
    }
    float32 nearest = 0.5f * (lo + hi);
    if (gap(nearest) > 0.0f)
        return std::nullopt;
    lo = 0.0f;
    for (int i = 0; i < 40; ++i)
    {
        const float32 mid = 0.5f * (lo + nearest);
        if (gap(mid) > 0.0f)
            lo = mid;
        else
            nearest = mid;
    }
    return nearest;
}

} // namespace

BodyHit shotHitsParts(const std::vector<BodyPart>& parts, const Math::Vector3& feet, float32 yaw,
                      const Math::Vector3& origin, const Math::Vector3& dir, float32 range)
{
    BodyHit hit;
    const BodyPart* best = nullptr;
    float32 bestT = range;
    for (const BodyPart& p : parts)
        if (const auto t = enterCapsule(p, origin, dir, range); t && *t <= bestT)
        {
            bestT = *t;
            best = &p;
        }
    if (!best)
        return hit;
    hit.zone = best->zone;
    hit.left = best->left;
    hit.distance = bestT;
    hit.point = origin + dir * bestT;
    const Math::Vector3 fwd{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    hit.fromFront = fwd.dot(Math::Vector3{origin.x - feet.x, 0.0f, origin.z - feet.z}) >= 0.0f;
    return hit;
}

Math::Vector3 aimPoint(const Math::Vector3& feet, bool head)
{
    return feet + Math::Vector3{0.0f, head ? 0.5f * (kNeckHeight + kHeadTop) : 0.5f * (kHipHeight + kNeckHeight), 0.0f};
}

bool lockOnHead(const Firearm& gun, float32 distance, bool wantHead)
{
    return distance < gun.autoHeadRange || (wantHead && distance < gun.headRange);
}

bool inLockonCone(const PlayerAim& aim, const Math::Vector3& eye, float32 yaw, float32 pitch, const Math::Vector3& target)
{
    const Math::Vector3 to = target - eye;
    const float32 dist = to.length();
    if (dist > aim.lockonDist)
        return false;
    const float32 flat = std::sqrt(to.x * to.x + to.z * to.z);
    if (flat < 1e-3f)
        return true;
    // Across: the angle between the view's heading and the target's.
    float32 dyaw = std::atan2(-to.x, -to.z) - yaw;
    while (dyaw > Math::pi)
        dyaw -= 2.0f * Math::pi;
    while (dyaw < -Math::pi)
        dyaw += 2.0f * Math::pi;
    const float32 dpitch = std::atan2(to.y, flat) - pitch;
    return std::fabs(dyaw) <= Math::radians(aim.lockonWidth) * 0.5f &&
           std::fabs(dpitch) <= Math::radians(aim.lockonHeight) * 0.5f;
}

float32 zoneDamage(const Firearm& gun, HitZone zone, float32 fullHealth, bool hardHead)
{
    switch (zone)
    {
    case HitZone::Head:
        if (gun.headShots > 0 && !hardHead)
            return fullHealth / static_cast<float32>(gun.headShots) + 1e-3f; // (so the last one kills)
        return gun.damage * gun.torsoMultiplier;
    case HitZone::Torso: return gun.damage * gun.torsoMultiplier;
    case HitZone::Arm: return gun.damage * gun.armMultiplier;
    case HitZone::Leg: return gun.damage * gun.legMultiplier;
    case HitZone::None: break;
    }
    return 0.0f;
}

Math::Vector3 spreadDirection(const Math::Vector3& dir, float32 spreadDegrees, std::mt19937& rng)
{
    if (spreadDegrees <= 0.0f)
        return dir;
    // Uniform over the cone's cap: the angle off the axis goes as the square root.
    std::uniform_real_distribution<float32> unit(0.0f, 1.0f);
    const float32 off = Math::radians(spreadDegrees) * 0.5f * std::sqrt(unit(rng));
    const float32 around = 2.0f * Math::pi * unit(rng);
    const Math::Vector3 helper = std::fabs(dir.y) < 0.9f ? Math::Vector3{0.0f, 1.0f, 0.0f} : Math::Vector3{1.0f, 0.0f, 0.0f};
    const Math::Vector3 u = dir.cross(helper).normalized();
    const Math::Vector3 v = dir.cross(u);
    return (dir * std::cos(off) + (u * std::cos(around) + v * std::sin(around)) * std::sin(off)).normalized();
}

void Recoil::kick(const Firearm& gun, float32 scale, bool right)
{
    float32 up = gun.recoilUp;
    float32 cap = gun.recoilUp;
    if (gun.maxRecoilUp > 0.0f)
    {
        cap = gun.maxRecoilUp;
        if (up_ > 0.0f)
            up += gun.recoilAccumulator; // still up from the last shot: it climbs
    }
    up_ = std::min(up_ + up * scale, std::max(cap * scale, up * scale));
    side_ = (right ? -1.0f : 1.0f) * gun.recoilSide * scale;
    const float32 time = std::max(gun.recoilTime, 1e-3f);
    upRate_ = up_ / time;
    sideRate_ = std::fabs(side_) / time;
}

void Recoil::update(float32 dt)
{
    up_ = std::max(0.0f, up_ - upRate_ * dt);
    const float32 back = sideRate_ * dt;
    side_ = std::fabs(side_) <= back ? 0.0f : side_ - (side_ > 0.0f ? back : -back);
}

float32 Recoil::pitch() const noexcept
{
    return Math::radians(up_);
}

float32 Recoil::yaw() const noexcept
{
    return Math::radians(side_);
}

int32 GunAmmo::loaded(const std::string& weapon) const
{
    const auto it = rounds_.find(weapon);
    return it == rounds_.end() ? 0 : it->second.first;
}

int32 GunAmmo::spare(const std::string& weapon) const
{
    const auto it = rounds_.find(weapon);
    return it == rounds_.end() ? 0 : it->second.second;
}

void GunAmmo::set(const Firearm& gun, const std::string& weapon, int32 rounds)
{
    auto& [clip, spare] = rounds_[weapon];
    rounds = std::clamp(rounds, 0, gun.maxRounds());
    clip = std::min(rounds, gun.clipAmmo);
    spare = rounds - clip;
}

bool GunAmmo::full(const Firearm& gun, const std::string& weapon) const
{
    return spare(weapon) >= gun.spareCap();
}

bool GunAmmo::takeRounds(const Firearm& gun, const std::string& weapon, int32 rounds)
{
    if (full(gun, weapon))
        return false;
    int32& spare = rounds_[weapon].second;
    spare = std::min(spare + std::max(0, rounds), gun.spareCap());
    return true;
}

bool GunAmmo::fire(const std::string& weapon)
{
    const auto it = rounds_.find(weapon);
    if (it == rounds_.end() || it->second.first <= 0)
        return false;
    --it->second.first;
    return true;
}

bool GunAmmo::canReload(const Firearm& gun, const std::string& weapon) const
{
    return loaded(weapon) < gun.clipAmmo && spare(weapon) > 0;
}

int32 GunAmmo::reload(const Firearm& gun, const std::string& weapon)
{
    auto& [clip, spare] = rounds_[weapon];
    const int32 moved = std::clamp(gun.clipAmmo - clip, 0, spare);
    clip += moved;
    spare -= moved;
    return moved;
}

bool GunAmmo::reloadOne(const Firearm& gun, const std::string& weapon)
{
    auto& [clip, spare] = rounds_[weapon];
    if (clip >= gun.clipAmmo || spare <= 0)
        return false;
    ++clip;
    --spare;
    return true;
}

const AmmoBox* ammoBox(const std::string& collectable)
{
    // The exe 0x463ed0, its jump table at 0x672714 (CT_AMMO_NAILS 102 .. CT_AMMO_SNIPER 107).
    static const AmmoBox kBoxes[] = {
        {"CT_AMMO_NAILS", 10, {"CT_NAILGUN", nullptr, nullptr}},
        {"CT_AMMO_SHOTGUN", 8, {"CT_SHOTGUN", "CT_SHOTGUN_TORCH", "CT_SAWNOFF"}},
        {"CT_AMMO_PISTOL", 12, {"CT_6SHOOTER", "CT_GLOCK", "CT_DESERT_EAGLE"}},
        {"CT_AMMO_MGUN", 30, {"CT_UZI", "CT_COLT_COMMANDO", nullptr}},
        {"CT_AMMO_TRANQ", 6, {"CT_TRANQ_RIFLE", nullptr, nullptr}},
        {"CT_AMMO_SNIPER", 6, {"CT_SNIPER_RIFLE", nullptr, nullptr}}, // (and CT 93, unnamed)
    };
    for (const AmmoBox& b : kBoxes)
        if (Assets::iequals(collectable, b.collectable))
            return &b;
    return nullptr;
}

} // namespace Game
