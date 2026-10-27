// Physics/collider.h - collision shapes: AABB, sphere, box; volume and position.
#pragma once
#include "engine_types.h"
#include "Math/math.h"
#include "Math/vector.h"

namespace Physics {

enum class ColliderType : uint32 {
    None = 0u,
    AABB = 1u,
    Sphere = 2u,
    Box = 3u,
};

// Axis-aligned bounding box.
struct AABB {
    Math::Vector3 min = Math::Vector3{0.0f, 0.0f, 0.0f};
    Math::Vector3 max = Math::Vector3{0.0f, 0.0f, 0.0f};

    [[nodiscard]] bool overlaps(const AABB& o) const noexcept {
        return min.x <= o.max.x && o.min.x <= max.x &&
               min.y <= o.max.y && o.min.y <= max.y &&
               min.z <= o.max.z && o.min.z <= max.z;
    }
    [[nodiscard]] Math::Vector3 size() const noexcept { return max - min; }
    [[nodiscard]] Math::Vector3 center() const noexcept { return (min + max) * 0.5f; }
    [[nodiscard]] float32 volume() const noexcept {
        const Math::Vector3 s = size();
        return s.x * s.y * s.z;
    }
};

// Collision shape with a type, a position, and shape-specific extents.
struct Collider {
    ColliderType type = ColliderType::None;
    Math::Vector3 pos = Math::Vector3{};
    Math::Vector3 halfExtent = Math::Vector3{0.5f, 0.5f, 0.5f};  // AABB / Box
    float32 radius = 0.5f;  // Sphere

    Collider() = default;
    explicit Collider(ColliderType t) : type(t) {}

    static Collider makeAABB(const Math::Vector3& p, const Math::Vector3& half) noexcept {
        Collider c(ColliderType::AABB);
        c.pos = p;
        c.halfExtent = half;
        return c;
    }
    static Collider makeSphere(const Math::Vector3& p, float32 r) noexcept {
        Collider c(ColliderType::Sphere);
        c.pos = p;
        c.radius = r;
        return c;
    }
    static Collider makeBox(const Math::Vector3& p, const Math::Vector3& half) noexcept {
        Collider c(ColliderType::Box);
        c.pos = p;
        c.halfExtent = half;
        return c;
    }

    [[nodiscard]] float32 volume() const noexcept;
    [[nodiscard]] AABB aabb() const noexcept;
    [[nodiscard]] Math::Vector3 position() const noexcept { return pos; }
    void setPosition(const Math::Vector3& p) noexcept { pos = p; }
};

} // namespace Physics
