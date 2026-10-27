// Physics/rigidbody.h - rigid body: position, velocity, mass, Euler integrate.
#pragma once
#include "engine_types.h"
#include "Math/math.h"
#include "Math/vector.h"
#include "Physics/collider.h"

namespace Physics {

// Rigid body with a position, velocity, mass, and gravity. Integrates position
// and velocity with semi-implicit Euler (the original binary's integration step).
class RigidBody {
public:
    RigidBody();

    [[nodiscard]] uint32 id() const noexcept { return id_; }

    // --- kinematics ---
    void setPosition(const Math::Vector3& p) noexcept { position_ = p; collider_.pos = p; }
    [[nodiscard]] Math::Vector3 position() const noexcept { return position_; }
    void setVelocity(const Math::Vector3& v) noexcept { velocity_ = v; }
    [[nodiscard]] Math::Vector3 velocity() const noexcept { return velocity_; }

    // --- dynamics ---
    void setMass(float32 m) noexcept { mass_ = m; }
    [[nodiscard]] float32 mass() const noexcept { return mass_; }
    void setGravity(const Math::Vector3& g) noexcept { gravity_ = g; }
    [[nodiscard]] Math::Vector3 gravity() const noexcept { return gravity_; }

    // --- collider ---
    void setCollider(const Collider& c) noexcept { collider_ = c; collider_.pos = position_; }
    [[nodiscard]] Collider const& collider() const noexcept { return collider_; }

    // Euler integration: v += a*dt; p += v*dt.
    void integrate(float32 dt) noexcept;

    [[nodiscard]] AABB boundingBox() const noexcept { return collider_.aabb(); }
    [[nodiscard]] float32 volume() const noexcept { return collider_.volume(); }

private:
    uint32 id_ = 0u;
    Math::Vector3 position_ = Math::Vector3{};
    Math::Vector3 velocity_ = Math::Vector3{};
    float32 mass_ = 1.0f;
    Math::Vector3 gravity_ = Math::Vector3{0.0f, -9.81f, 0.0f};
    Collider collider_;
};

} // namespace Physics
