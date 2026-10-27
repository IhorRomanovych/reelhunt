// Game/entity.h - a live game-world entity (transform + health + owner).
//
// This is the data the save system restores and writes back (see save_restore.h). Behaviour
// (AI, animation, scripting) attaches to entities in the gameplay phases (PLAN.md 9b).
#pragma once
#include "engine_types.h"
#include "renderware_api_stub.h"
#include "Math/quaternion.h"
#include "Math/transform.h"
#include "Math/vector.h"

namespace Game {

class GameEntity final : public Engine::Rw::Entity {
public:
    explicit GameEntity(uint32 type, uint32 owner = 0u) noexcept
        : Engine::Rw::Entity(type), owner_(owner) {}

    [[nodiscard]] uint32 owner() const noexcept { return owner_; }
    void setOwner(uint32 o) noexcept { owner_ = o; }

    [[nodiscard]] const Math::Transform& transform() const noexcept { return transform_; }
    void setTransform(const Math::Transform& t) noexcept { transform_ = t; }
    void setTranslation(const Math::Vector3& p) noexcept { transform_.setTranslation(p); }
    void setRotation(const Math::Quaternion& q) noexcept { transform_.setRotation(q); }
    void moveBy(const Math::Vector3& delta) noexcept { transform_.position += delta; }

    [[nodiscard]] uint32 health() const noexcept { return health_; }
    void setHealth(uint32 h) noexcept { health_ = h; }
    void damage(uint32 amount) noexcept { health_ = health_ > amount ? health_ - amount : 0u; }
    [[nodiscard]] bool isAlive() const noexcept { return health_ > 0u; }

private:
    uint32 owner_ = 0u;
    Math::Transform transform_;
    uint32 health_ = 100u;
};

} // namespace Game
