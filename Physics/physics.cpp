// Physics/physics.cpp - physics engine step, integration, and collision.
#include "engine_types.h"
#include "Physics/physics.h"

namespace Physics {

PhysicsEngine::PhysicsEngine() = default;

PhysicsEngine::~PhysicsEngine() = default;

void PhysicsEngine::addBody(std::unique_ptr<RigidBody> b) noexcept {
    if (!b) return;
    b->setGravity(gravity_);
    bodies_.push_back(std::move(b));
}

void PhysicsEngine::removeBody(uint32 id) noexcept {
    for (auto it = bodies_.begin(); it != bodies_.end(); ++it) {
        if ((*it)->id() == id) {
            bodies_.erase(it);
            return;
        }
    }
}

void PhysicsEngine::integrateBodies(float32 dt) noexcept {
    for (auto& body : bodies_) {
        if (body) {
            body->integrate(dt);
        }
    }
}

std::vector<Collision> PhysicsEngine::detectCollisions() noexcept {
    std::vector<RigidBody*> pointers;
    pointers.reserve(bodies_.size());
    for (auto& body : bodies_) {
        if (body) {
            pointers.push_back(body.get());
        }
    }
    return CollisionDetector::detect(pointers);
}

void PhysicsEngine::step(float32 dt) noexcept {
    integrateBodies(dt);
    (void)detectCollisions();
}

} // namespace Physics
