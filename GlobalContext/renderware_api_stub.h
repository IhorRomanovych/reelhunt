// GlobalContext/renderware_api_stub.h - the RenderWare-style object base shared by every
// scene object (meshes, cameras, lights, game entities).
#pragma once
#include "engine_types.h"

namespace Engine {
namespace Rw {

// Base for every scene object. Each instance receives a process-unique, non-zero id at
// construction; the scene and the asset manager look objects up by that id.
class Entity {
public:
    explicit Entity(uint32 type) noexcept;
    virtual ~Entity() = default;

    Entity(const Entity&) = delete;
    Entity& operator=(const Entity&) = delete;

    [[nodiscard]] virtual uint32 type() const noexcept { return type_; }
    [[nodiscard]] virtual bool active() const noexcept { return hasFlag(flags_, EntityFlag::Active); }
    void setActive(bool v) noexcept;
    void setFlags(EntityFlag f) noexcept { flags_ = f; }
    [[nodiscard]] EntityFlag flags() const noexcept { return flags_; }
    [[nodiscard]] ObjectId id() const noexcept { return id_; }

protected:
    uint32 type_;

private:
    ObjectId id_;
    EntityFlag flags_ = EntityFlag::None;
};

} // namespace Rw
} // namespace Engine
