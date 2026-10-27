// Core/asset_manager.cpp - level-scoped asset lifecycle.
#include "Core/asset_manager.h"
#include "Assets/sfx_bank.h"
#include "Game/level_content.h"
#include "Renderer/backend/vulkan.h"
#include "Renderer/mesh.h"
#include "Renderer/scene.h"

#include <iostream>

AssetManager::AssetManager() = default;
AssetManager::~AssetManager() = default;

void AssetManager::loadSharedAssets(const std::string& gameRoot)
{
    if (!sfxBank_)
        sfxBank_ = std::make_unique<Assets::SfxBank>();
    const std::string sfxDir = gameRoot + "audio/PC/SFX";
    if (sfxBank_->load(sfxDir))
        std::cout << "[assets] SFX bank loaded: " << sfxBank_->sampleCount() << " samples\n";
    else
        std::cerr << "[assets] WARNING: could not load SFX bank from " << sfxDir << "\n";
}

void AssetManager::loadLevel(const std::string& levelDir, Renderer::Scene& scene, Renderer::Backend::Vulkan* device,
                             const std::vector<Game::SaveEntityRecord>* checkpoint)
{
    unloadLevel(scene, device);
    level_ = std::make_unique<Game::LevelContent>();
    level_->load(scene, levelDir, checkpoint);
    std::cout << "[assets] level loaded: " << levelDir << " entities=" << level_->entityIds().size() << "\n";
}

void AssetManager::unloadLevel(Renderer::Scene& scene, Renderer::Backend::Vulkan* device)
{
    if (!level_)
        return;

    // GPU resources first, while the Mesh objects (the cache keys) still exist.
    if (device)
    {
        for (ObjectId id : level_->entityIds())
            if (const auto* mesh = dynamic_cast<const Renderer::Mesh*>(scene.find(id)))
                device->removeMesh(mesh);
        for (uint32 h : level_->textureHandles())
            device->removeTexture(h);
    }
    scene.removeEntities(level_->entityIds());

    std::cout << "[assets] level unloaded: freed " << level_->entityIds().size() << " entities\n";
    level_.reset();
}
