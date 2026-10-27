// Physics/collider.cpp - collision shape volume and AABB construction.
#include "engine_types.h"
#include "Physics/collider.h"
#include <cmath>

namespace Physics {

float32 Collider::volume() const noexcept {
    switch (type) {
        case ColliderType::Sphere:
            return (4.0f / 3.0f) * Math::pi * radius * radius * radius;
        case ColliderType::Box:
        case ColliderType::AABB:
            return (2.0f * halfExtent.x) * (2.0f * halfExtent.y) * (2.0f * halfExtent.z);
        default:
            return 0.0f;
    }
}

AABB Collider::aabb() const noexcept {
    AABB box;
    switch (type) {
        case ColliderType::Sphere:
            box.min = pos - Math::Vector3{radius, radius, radius};
            box.max = pos + Math::Vector3{radius, radius, radius};
            break;
        case ColliderType::Box:
        case ColliderType::AABB:
            box.min = pos - halfExtent;
            box.max = pos + halfExtent;
            break;
        default:
            box.min = pos;
            box.max = pos;
            break;
    }
    return box;
}

} // namespace Physics
