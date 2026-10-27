// Physics/rigidbody.cpp - rigid body integration.
#include "engine_types.h"
#include "Physics/rigidbody.h"

namespace {
uint32 nextRigidBodyId() noexcept {
    static uint32 next = 1u;
    return next++;
}
} // namespace anonymous

namespace Physics {

RigidBody::RigidBody() : id_(nextRigidBodyId()) {}

void RigidBody::integrate(float32 dt) noexcept {
    // Semi-implicit Euler:
    //   v = v + a * dt
    //   p = p + v * dt
    velocity_ = velocity_ + gravity_ * dt;
    position_ = position_ + velocity_ * dt;
    collider_.pos = position_;
}

} // namespace Physics
