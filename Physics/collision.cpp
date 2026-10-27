// Physics/collision.cpp - broad-phase + narrow-phase collision detection.
#include "engine_types.h"
#include "Physics/collision.h"
#include "Physics/rigidbody.h"
#include <algorithm>
#include <cmath>

namespace Physics {

bool CollisionDetector::testAABB(const AABB& a, const AABB& b) noexcept {
    return a.overlaps(b);
}

bool CollisionDetector::testSphere(const Math::Vector3& ca, float32 ra,
                                   const Math::Vector3& cb, float32 rb,
                                   Math::Vector3* outNormal,
                                   float32* outPenetration) noexcept {
    const Math::Vector3 d = ca - cb;
    const float32 distSq = d.lengthSquared();
    const float32 sum = ra + rb;
    if (distSq > sum * sum) {
        return false;
    }
    const float32 dist = std::sqrt(distSq);
    if (outNormal) {
        if (dist > 0.0f) {
            *outNormal = d * (1.0f / dist);
        } else {
            *outNormal = Math::Vector3{0.0f, 1.0f, 0.0f};
        }
    }
    if (outPenetration) {
        *outPenetration = sum - dist;
    }
    return true;
}

bool CollisionDetector::testAABBvsSphere(const AABB& a, const Math::Vector3& c, float32 r,
                                         Math::Vector3* outNormal,
                                         float32* outPenetration) noexcept {
    const float32 clampedX = std::max(a.min.x, std::min(c.x, a.max.x));
    const float32 clampedY = std::max(a.min.y, std::min(c.y, a.max.y));
    const float32 clampedZ = std::max(a.min.z, std::min(c.z, a.max.z));
    const Math::Vector3 closest{clampedX, clampedY, clampedZ};
    const Math::Vector3 d = c - closest;
    const float32 distSq = d.lengthSquared();
    if (distSq > r * r) {
        return false;
    }
    const float32 dist = std::sqrt(distSq);
    if (outNormal) {
        if (dist > 0.0f) {
            *outNormal = d * (1.0f / dist);
        } else {
            *outNormal = Math::Vector3{0.0f, 1.0f, 0.0f};
        }
    }
    if (outPenetration) {
        *outPenetration = r - dist;
    }
    return true;
}

bool CollisionDetector::testPair(RigidBody* a, RigidBody* b, Collision* out) noexcept {
    if (!a || !b) {
        return false;
    }
    const Collider& ca = a->collider();
    const Collider& cb = b->collider();

    switch (ca.type) {
        case ColliderType::Sphere:
            switch (cb.type) {
                case ColliderType::Sphere:
                    return testSphere(ca.position(), ca.radius, cb.position(), cb.radius,
                                      &out->normal, &out->penetration);
                case ColliderType::AABB:
                case ColliderType::Box: {
                    // The sphere is `a`: test a's centre/radius against b's box (previously this
                    // used b's position and radius, i.e. tested the box against itself).
                    return testAABBvsSphere(cb.aabb(), ca.position(), ca.radius,
                                            &out->normal, &out->penetration);
                }
                default:
                    return false;
            }
        case ColliderType::AABB:
        case ColliderType::Box:
            switch (cb.type) {
                case ColliderType::Sphere: {
                    const bool hit = testAABBvsSphere(ca.aabb(), cb.position(), cb.radius,
                                                      &out->normal, &out->penetration);
                    out->normal = out->normal * -1.0f; // box->sphere is a->b; report b->a
                    return hit;
                }
                case ColliderType::AABB:
                case ColliderType::Box:
                    return testAABB(ca.aabb(), cb.aabb());
                default:
                    return false;
            }
        default:
            return false;
    }
}

std::vector<Collision> CollisionDetector::detect(const std::vector<RigidBody*>& bodies) noexcept {
    std::vector<Collision> results;
    for (size_t i = 0u; i < bodies.size(); ++i) {
        for (size_t j = i + 1u; j < bodies.size(); ++j) {
            if (!testAABB(bodies[i]->boundingBox(), bodies[j]->boundingBox())) {
                continue;
            }
            Collision c;
            c.a = bodies[i];
            c.b = bodies[j];
            // Contact point: midpoint of the two collider centers.
            c.contactPoint = (bodies[i]->collider().position() + bodies[j]->collider().position()) * 0.5f;
            if (testPair(bodies[i], bodies[j], &c)) {
                results.push_back(std::move(c));
            }
        }
    }
    return results;
}

} // namespace Physics
