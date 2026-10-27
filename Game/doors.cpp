// Game/doors.cpp - swinging doors. See doors.h.
#include "Game/doors.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iostream>
#include <utility>

#include "Math/math.h"
#include "Renderer/mesh.h"

namespace Game {

namespace {

// SWINGDOOR_PHYS (physicsTypeData.ini).
constexpr float32 kDoorMass = 20.0f;
constexpr float32 kHingeBounce = 0.2f;
constexpr float32 kSpringStrength = 0.15f; // x MASS x the angle
constexpr float32 kSpringDamp = 0.08f;     // x the turn
constexpr bool kSwingDoor = false;         // SWING_DOOR 0: it latches when it swings shut
constexpr float32 kOverrideGain = 2.5f;    // 0x724944
constexpr float32 kPushReach = 0.15f;      // a body this close to a door touches it (ours)

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

Math::Vector3 horizontal(const Math::Vector3& v) { return {v.x, 0.0f, v.z}; }

} // namespace

void Doors::build(const LevelContent& level)
{
    doors_.clear();
    const LevelFrame& frame = level.frame();
    for (const LevelContent::DoorModel& m : level.doors())
    {
        if (!m.placement || m.meshes.empty())
            continue;
        Door d;
        d.name = lower(m.placement->internalName);
        d.record = lower(m.placement->record);
        d.hinge = frame.placementOrigin(m.placement->position);
        d.toLocal = LevelFrame::placementRotation(m.placement->rotation).transpose();
        d.localMin = m.localMin;
        d.localMax = m.localMax;
        d.meshes = m.meshes;
        d.slide = m.slide;
        if (d.slide)
        {
            // Along its width (model X), back over its origin when the model lies on +X.
            const float32 sign = d.localMax.x > -d.localMin.x ? -1.0f : 1.0f;
            d.slideAxis = d.toLocal.transpose().multiplyPoint(Math::Vector3{sign, 0.0f, 0.0f});
            d.frozen = false;
        }
        doors_.push_back(std::move(d));
    }
    std::cerr << "doors: " << doors_.size();
    for (const Door& d : doors_)
        std::cerr << " " << d.name << " at (" << d.hinge.x << ", " << d.hinge.y << ", " << d.hinge.z << ") "
                  << d.localMax.x - d.localMin.x << " m wide;";
    std::cerr << "\n";
}

Doors::Door* Doors::find(const std::string& name)
{
    const std::string key = lower(name);
    for (Door& d : doors_)
        if (d.name == key)
            return &d;
    return nullptr;
}

Math::Vector3 Doors::closestPoint(const Door& door, const Math::Vector3& p) const
{
    // Undo the swing, then the placement: model space, where the box is axis-aligned.
    const Math::Vector3 closed = door.slide ? (p - door.slideAxis * door.offset) - door.hinge
                                            : Math::Matrix4x4::rotationY(-door.angle).multiplyPoint(p - door.hinge);
    const Math::Vector3 local = door.toLocal.multiplyPoint(closed);
    const Math::Vector3 c{std::clamp(local.x, door.localMin.x, door.localMax.x), std::clamp(local.y, door.localMin.y, door.localMax.y),
                          std::clamp(local.z, door.localMin.z, door.localMax.z)};
    const Math::Vector3 back = door.toLocal.transpose().multiplyPoint(c);
    return Math::Matrix4x4::rotationY(door.angle).multiplyPoint(back) + door.hinge;
}

Math::Vector3 Doors::closedCentre(const Door& door)
{
    return door.toLocal.transpose().multiplyPoint((door.localMin + door.localMax) * 0.5f) + door.hinge;
}

float32 Doors::energy(const Door& door)
{
    return 0.75f * 90.0f * inertia(door) * door.speed * door.speed / kDoorMass;
}

float32 Doors::inertia(const Door& door)
{
    const float32 w = door.localMax.x - door.localMin.x, d = door.localMax.z - door.localMin.z;
    return std::max(kDoorMass * (w * w + d * d) / 12.0f, 1e-3f);
}

void Doors::place(Door& door) const
{
    const Math::Matrix4x4 m = door.slide ? Math::Matrix4x4::translation(door.slideAxis * door.offset)
                                         : Math::Matrix4x4::translation(door.hinge) * Math::Matrix4x4::rotationY(door.angle) *
                                               Math::Matrix4x4::translation(door.hinge * -1.0f);
    for (Renderer::Mesh* mesh : door.meshes)
        mesh->setTransform(m);
}

void Doors::update(float32 dt, const std::vector<Pusher>& pushers)
{
    for (Door& d : doors_)
    {
        if (d.destroyed)
            continue;
        if (d.slide)
        {
            const float32 was = d.offset;
            const float32 step = d.slideSpeed * dt;
            d.offset += std::clamp(d.target - d.offset, -step, step);
            if (d.offset != was)
                place(d);
            // Off it goes, and there it is (0x4cfcb0: opening / closing, open / closed).
            const std::size_t index = static_cast<std::size_t>(&d - doors_.data());
            if (d.offset != d.target && !d.sliding)
            {
                d.sliding = true;
                sounds_.push_back({index, 0x21, 0.0f});
            }
            else if (d.offset == d.target && d.sliding)
            {
                // (posted twice: both play)
                d.sliding = false;
                sounds_.push_back({index, 0x22, 0.0f});
                sounds_.push_back({index, 0x22, 0.0f});
            }
            continue;
        }
        const float32 before = d.angle;
        const float32 I = inertia(d);
        // Driven to SetDoorOverrideAngle: until it is that far open, then it stops and sleeps.
        if (d.driving && d.overrideAngle)
        {
            d.awake = true;
            if (std::fabs(*d.overrideAngle) <= std::fabs(d.angle))
            {
                d.driving = d.awake = false;
                d.speed = 0.0f;
            }
            else
                d.speed += dt * kOverrideGain * (*d.overrideAngle - d.angle) / I;
        }
        // Frozen or locked, only the drive moves it.
        const bool free = !d.frozen && !d.locked;
        if (!free && !d.driving)
            d.speed = 0.0f;
        else if (free && d.awake && !d.driving) // its spring back to shut
            d.speed += dt * -(kSpringStrength * kDoorMass * d.angle + kSpringDamp * d.speed) / I;
        if (free)
        {
            // Bodies against it: the impulse that stops the contact closing (BOUNCE 0).
            for (const Pusher& p : pushers)
            {
                const Math::Vector3 c = closestPoint(d, p.center);
                const Math::Vector3 toDoor = horizontal(c - p.center);
                const float32 dist = toDoor.length();
                const Math::Vector3 r = horizontal(c - d.hinge);
                const float32 arm = r.length();
                if (dist > p.radius + kPushReach || arm < 0.2f || dist < 1e-4f)
                    continue;
                const Math::Vector3 n = toDoor * (1.0f / dist); // from the body into the door
                // Which way a positive swing moves the contact point.
                const Math::Vector3 tangent = horizontal(Math::Matrix4x4::rotationY(0.01f).multiplyPoint(r) - r).normalized();
                const float32 share = arm * tangent.dot(n); // the turn an impulse along n makes, x I
                const float32 vn = (tangent * (d.speed * arm) - horizontal(p.velocity)).dot(n);
                if (vn >= 0.0f || std::fabs(share) < 1e-4f)
                    continue;
                const float32 j = -vn / (1.0f / kDoorMass + 1.0f / std::max(p.mass, 1e-3f) + share * share / I);
                if (!d.awake)
                    std::cerr << "doors: " << d.name << " pushed at " << Math::degrees(d.angle) << " deg\n";
                d.speed += j * share / I;
                d.awake = true;
                // Pushed while shut (0x4907b0 -> 0x4cc3e0).
                if (d.angle == 0.0f)
                    sounds_.push_back({static_cast<std::size_t>(&d - doors_.data()), 0x1b, energy(d) * 4.0f});
            }
        }
        d.angle += d.speed * dt;
        if (free)
        {
            // Past an open limit it bounces back off it; swung shut, it latches. A limit hit is heard
            // (0x4cc430) only once it has swung back 0.3 rad (0x7387cc) from a limit since the last
            // (0x4cc4b0 arms +0x188).
            const float32 last = d.lastAngle;
            constexpr float32 kRearm = 0.3f;
            if ((last > d.openIn - kRearm && d.angle < d.openIn - kRearm) || (last < kRearm - d.openOut && d.angle > kRearm - d.openOut))
                d.limitArmed = true;
            if ((d.angle >= d.openIn && last < d.openIn) || (d.angle <= -d.openOut && last > -d.openOut))
            {
                if (std::exchange(d.limitArmed, false))
                    sounds_.push_back({static_cast<std::size_t>(&d - doors_.data()), 0x20, energy(d)});
                d.angle = std::clamp(d.angle, -d.openOut, d.openIn); // (ours: not past it)
                d.speed = -d.speed * kHingeBounce;
            }
            else if (!kSwingDoor && !d.driving && ((d.angle >= 0.0f && last < 0.0f) || (d.angle <= 0.0f && last > 0.0f)))
            {
                sounds_.push_back({static_cast<std::size_t>(&d - doors_.data()), 0x1c, energy(d) * 4.0f});
                d.angle = d.speed = 0.0f;
                d.awake = false;
            }
        }
        d.lastAngle = d.angle;
        if (d.angle != before)
            place(d);
    }
}

bool Doors::collide(Math::Vector3& center, float32 radius, Math::Vector3& velocity) const
{
    bool touched = false;
    for (const Door& d : doors_)
    {
        if (d.destroyed)
            continue;
        const Math::Vector3 c = closestPoint(d, center);
        Math::Vector3 away = horizontal(center - c); // doors are walls: push sideways only
        const float32 dist = away.length();
        if (dist >= radius || std::fabs(center.y - c.y) >= radius)
            continue;
        if (dist < 1e-4f)
            continue; // the centre is inside the box: no direction to push in
        away = away * (1.0f / dist);
        center += away * (radius - dist);
        if (const float32 vn = velocity.dot(away); vn < 0.0f)
            velocity -= away * vn;
        touched = true;
    }
    return touched;
}

} // namespace Game
