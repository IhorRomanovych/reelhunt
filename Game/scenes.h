// Game/scenes.h - the game's scene (level) list.
//
// initscripts/LEVELS/levels.txt lists the level folders in story order, and the game text
// names them: GXT key LVL_<n> (+ LVL_<n>A, a second line or "-") is scene n. Scene 1 "BORN
// AGAIN" is Jury_Turf; scene 11 "MOUTH OF MADNESS" is Asylum; scenes 1-20 are the story and the
// rest are bonus levels.
#pragma once
#include "engine_types.h"

#include <expected>
#include <string>
#include <vector>

namespace Assets { class Gxt; }

namespace Game {

struct SceneInfo {
    int number = 0;          // 1-based scene number
    std::string folder;      // level folder under levels/, lower-case (e.g. "jury_turf")
    std::string title;       // display name from the game text, e.g. "BORN AGAIN"
};

inline constexpr int kStoryScenes = 20;

// Parses levels.txt ("LEVEL <Folder> <mini-map numbers...>" lines). Titles come from `gxt` when
// given, otherwise they are left empty.
std::expected<std::vector<SceneInfo>, Engine::EngineError> loadScenes(const std::string& levelsTxtPath,
                                                                      const Assets::Gxt* gxt);

// The scene number a save slot title refers to ("1)SCENE 1-(IN PROGRESS)" -> 1), or 0.
int sceneNumberFromSaveTitle(const std::string& title);

} // namespace Game
