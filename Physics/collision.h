// Physics/collision.h - collision detection: AABB/AABB, sphere/sphere, AABB/sphere.
#pragma once
#include "engine_types.h"
#include <vector>
#include "Math/math.h"
#include "Math/vector.h"
#include "Physics/collider.h"

namespace Physics {

class RigidBody;

// A collision event generated between two bodies. `normal` points from b towards a (the
// direction that separates a from b); `penetration` is the overlap depth along it.
struct Collision {
    RigidBody* a = nullptr;
    RigidBody* b = nullptr;
    Math::Vector3 contactPoint = Math::Vector3{};
    Math::Vector3 normal = Math::Vector3{0.0f, 1.0f, 0.0f};
    float32 penetration = 0.0f;
};

// Collision detection: broad-phase AABB overlap followed by narrow-phase shape
// tests (AABB/AABB, sphere/sphere, AABB/sphere), producing Collision events.
class CollisionDetector {
public:
    static std::vector<Collision> detect(const std::vector<RigidBody*>& bodies) noexcept;

    // --- narrow-phase tests ---
    static bool testAABB(const AABB& a, const AABB& b) noexcept;

    static bool testSphere(const Math::Vector3& ca, float32 ra,
                           const Math::Vector3& cb, float32 rb,
                           Math::Vector3* outNormal = nullptr,
                           float32* outPenetration = nullptr) noexcept;

    static bool testAABBvsSphere(const AABB& a, const Math::Vector3& c, float32 r,
                                 Math::Vector3* outNormal = nullptr,
                                 float32* outPenetration = nullptr) noexcept;

private:
    static bool testPair(RigidBody* a, RigidBody* b, Collision* out) noexcept;
};

} // namespace Physics
