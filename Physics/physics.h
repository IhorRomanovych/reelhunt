// Physics/physics.h - physics engine: step, add/remove bodies, gravity.
#pragma once
#include "engine_types.h"
#include <memory>
#include <vector>
#include "Math/math.h"
#include "Math/vector.h"
#include "Physics/collider.h"
#include "Physics/rigidbody.h"
#include "Physics/collision.h"

namespace Physics {

// Physics engine. Owns a set of rigid bodies, applies gravity, integrates
// each body with semi-implicit Euler, and detects collisions between bodies.
class PhysicsEngine {
public:
    PhysicsEngine();
    ~PhysicsEngine();

    PhysicsEngine(PhysicsEngine const&) = delete;
    PhysicsEngine& operator=(PhysicsEngine const&) = delete;

    // --- gravity ---
    void setGravity(const Math::Vector3& g) noexcept { gravity_ = g; }
    [[nodiscard]] Math::Vector3 gravity() const noexcept { return gravity_; }

    // --- bodies ---
    void addBody(std::unique_ptr<RigidBody> b) noexcept;
    void removeBody(uint32 id) noexcept;
    [[nodiscard]] std::size_t bodyCount() const noexcept { return bodies_.size(); }
    [[nodiscard]] const std::vector<std::unique_ptr<RigidBody>>& bodies() const noexcept {
        return bodies_;
    }

    // --- frame step: integrate all bodies, then detect collisions ---
    void step(float32 dt) noexcept;
    void integrateBodies(float32 dt) noexcept;
    [[nodiscard]] std::vector<Collision> detectCollisions() noexcept;

private:
    Math::Vector3 gravity_ = Math::Vector3{0.0f, -9.81f, 0.0f};
    std::vector<std::unique_ptr<RigidBody>> bodies_;
};

} // namespace Physics
