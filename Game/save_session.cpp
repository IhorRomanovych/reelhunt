// Game/save_session.cpp - an original *.SAV restored into live entities, and written back.
#include "Game/save_session.h"

#include "Game/entity.h"
#include "Game/save_restore.h"
#include "Renderer/scene.h"

#include <array>
#include <iostream>
#include <memory>

namespace Game {

bool SaveSession::load(const std::string& path, Renderer::Scene& scene)
{
    if (!loadSaveFile(path, save_))
    {
        std::cerr << "save: could not load " << path << " (continuing without a save)\n";
        return false;
    }

    const auto world = restoreEntities(save_);
    live_.assign(save_.entities.size(), nullptr);
    std::array<std::size_t, 9> byKind{};
    for (std::size_t i = 0; i < world.size(); ++i)
    {
        const RestoredEntity& re = world[i];
        if (save_.entities[i].name == "NUL") // null marker: no entity
            continue;
        ++byKind[static_cast<std::size_t>(re.state.kind)];
        auto ent = std::make_unique<GameEntity>(typeForKind(re.state.kind), 0u);
        applyToEntity(*ent, re);
        live_[i] = ent.get(); // the scene owns it
        if (re.state.kind == EntityKind::Player)
            player_ = ent.get();
        scene.addEntity(std::move(ent));
    }
    loaded_ = true;

    std::cerr << "save: loaded " << path << " (" << save_.entities.size() << " records, title '" << save_.title() << "')"
              << " breakable=" << byKind[static_cast<std::size_t>(EntityKind::Breakable)]
              << " dynamic=" << byKind[static_cast<std::size_t>(EntityKind::Dynamic)]
              << " light=" << byKind[static_cast<std::size_t>(EntityKind::Light)]
              << " camera=" << byKind[static_cast<std::size_t>(EntityKind::CameraData)]
              << " corpse=" << byKind[static_cast<std::size_t>(EntityKind::Corpse)]
              << " savepoint=" << byKind[static_cast<std::size_t>(EntityKind::SavePoint)]
              << " trigger=" << byKind[static_cast<std::size_t>(EntityKind::Trigger)]
              << " unknown=" << byKind[static_cast<std::size_t>(EntityKind::Unknown)] << "\n";
    if (player_)
    {
        const auto p = player_->transform().position;
        std::cerr << "  player=(" << p.x << "," << p.y << "," << p.z << ") health=" << player_->health() << "\n";
    }
    return true;
}

bool SaveSession::write(const std::string& path)
{
    if (!loaded_)
    {
        std::cerr << "save: no save loaded; nothing to write\n";
        return false;
    }
    std::size_t updated = 0;
    for (std::size_t i = 0; i < live_.size(); ++i)
        if (live_[i])
        {
            writeBack(save_.entities[i], *live_[i]);
            ++updated;
        }
    if (!writeSaveFile(path, save_))
    {
        std::cerr << "save: FAILED to write " << path << "\n";
        return false;
    }
    std::cerr << "save: wrote " << path << " (" << updated << " entities updated)\n";
    return true;
}

} // namespace Game
