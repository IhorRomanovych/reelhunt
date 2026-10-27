// Renderer/scene.h - the scene: an owning list of entities (meshes, cameras, lights, game entities).
#pragma once
#include "engine_types.h"
#include "renderware_api_stub.h"
#include "Renderer/lighting.h"
#include "Renderer/screen_effects.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace Renderer {

class Camera;

// Owns every entity added to it. Lookup by id is O(1); iteration order is insertion order
// (the renderer draws in that order).
class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    Scene(Scene const&) = delete;
    Scene& operator=(Scene const&) = delete;

    // Takes ownership; returns a non-owning pointer to the stored entity (nullptr if `e` was null).
    Engine::Rw::Entity* addEntity(std::unique_ptr<Engine::Rw::Entity> e);
    void removeEntity(ObjectId id);
    // Removes every entity whose id is in `ids` in a single pass.
    void removeEntities(const std::vector<ObjectId>& ids);

    [[nodiscard]] uint32 count() const noexcept { return static_cast<uint32>(entities_.size()); }
    [[nodiscard]] Engine::Rw::Entity* find(ObjectId id) const noexcept;
    [[nodiscard]] const std::vector<std::unique_ptr<Engine::Rw::Entity>>& entities() const noexcept {
        return entities_;
    }

    // The camera the renderer draws through (the first camera added unless set explicitly).
    void setActiveCamera(const Camera* cam) noexcept { activeCamera_ = cam; }
    [[nodiscard]] const Camera* activeCamera() const noexcept { return activeCamera_; }

    // How the scene is lit (the level sets its own; Midday by default).
    void setLighting(const SceneLighting& l) noexcept { lighting_ = l; }
    [[nodiscard]] const SceneLighting& lighting() const noexcept { return lighting_; }
    [[nodiscard]] SceneLighting& lighting() noexcept { return lighting_; }
    // Full-screen effects for the final composite (fade, flash, camcorder look).
    [[nodiscard]] const ScreenEffects& effects() const noexcept { return effects_; }
    [[nodiscard]] ScreenEffects& effects() noexcept { return effects_; }

private:
    std::vector<std::unique_ptr<Engine::Rw::Entity>> entities_;
    std::unordered_map<ObjectId, Engine::Rw::Entity*> byId_;
    const Camera* activeCamera_ = nullptr;
    SceneLighting lighting_;
    ScreenEffects effects_;
};

} // namespace Renderer
