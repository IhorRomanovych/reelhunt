// Game/save_session.h - an original *.SAV restored into live entities, and written back.
#pragma once
#include "engine_types.h"

#include <string>
#include <vector>

#include "Game/save_system.h"

namespace Renderer { class Scene; }

namespace Game {

class GameEntity;

class SaveSession {
public:
    // Load `path` and spawn a live GameEntity for every saved entity (the "NUL" markers excepted):
    // the player entity gets the saved transform + health; the rest are data-only placeholders.
    // Returns false (and spawns nothing) if the save cannot be read.
    bool load(const std::string& path, Renderer::Scene& scene);

    // Capture every live entity back into its record and write a byte-compatible save.
    bool write(const std::string& path);

    [[nodiscard]] bool isLoaded() const noexcept { return loaded_; }
    // The loaded save itself (buffers the live entities do not cover, e.g. the inventory).
    [[nodiscard]] SaveFile& file() noexcept { return save_; }
    [[nodiscard]] GameEntity* player() const noexcept { return player_; }

private:
    SaveFile save_;
    std::vector<GameEntity*> live_; // aligned with save_.entities; nullptr for "NUL" markers
    GameEntity* player_ = nullptr;
    bool loaded_ = false;
};

} // namespace Game
