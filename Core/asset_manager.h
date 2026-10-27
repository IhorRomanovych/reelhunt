// Core/asset_manager.h - level-scoped asset lifecycle.
// Owns the current level's LevelContent and frees its scene meshes + GPU resources on unload.
// Shared assets (the SFX bank) persist across level transitions.
#pragma once
#include "engine_types.h"

#include <memory>
#include <string>
#include <vector>

namespace Renderer { class Scene; }
namespace Renderer::Backend { class Vulkan; }
namespace Game { class LevelContent; struct SaveEntityRecord; }
namespace Assets { class SfxBank; }

class AssetManager {
public:
    AssetManager();
    ~AssetManager();

    AssetManager(const AssetManager&) = delete;
    AssetManager& operator=(const AssetManager&) = delete;

    // Load shared assets that persist across levels (SFX bank). Call once at startup.
    void loadSharedAssets(const std::string& gameRoot);

    // Load a level into the scene, unloading the previous one first.
    // `checkpoint`: build the level's entities from a save point's entity block (Game/checkpoint.h).
    void loadLevel(const std::string& levelDir, Renderer::Scene& scene, Renderer::Backend::Vulkan* device,
                   const std::vector<Game::SaveEntityRecord>* checkpoint = nullptr);

    // Remove the level's meshes from the scene and free their GPU buffers/images. Must be called
    // while the scene and device are alive (the destructor cannot do it for you).
    void unloadLevel(Renderer::Scene& scene, Renderer::Backend::Vulkan* device);

    [[nodiscard]] bool hasLevel() const noexcept { return level_ != nullptr; }
    [[nodiscard]] Game::LevelContent* level() noexcept { return level_.get(); }
    [[nodiscard]] Assets::SfxBank* sfxBank() noexcept { return sfxBank_.get(); }

private:
    std::unique_ptr<Game::LevelContent> level_;
    std::unique_ptr<Assets::SfxBank> sfxBank_;
};
