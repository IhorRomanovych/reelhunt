// Game/app_config.h - runtime configuration: data paths and launch mode.
//
// Every path the engine reads or writes comes from here, so nothing is hardcoded to one
// machine. Defaults come from the build (the game data next to the source tree, see
// CMakeLists.txt) and can be overridden on the command line; run `main --help` for the list.
#pragma once
#include "engine_types.h"
#include "Game/settings.h"
#include "Renderer/lighting.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace Game {

struct AppConfig {
    // The player's key bindings (Settings.dat's first table, Game/bindings.h); null: the defaults.
    std::array<uint8, 0x130>* bindings = nullptr;
    std::string gameRoot;        // The game install / data root, with a trailing slash
    std::string level = "jury_turf";
    std::string characterDff;    // empty = levels/GLOBAL/CHARPAK/cash_pc.dff under gameRoot
    std::string characterTxd;    // empty = next to the DFF
    std::string animations;      // empty = levels/<level>/allanims.ifp
    std::string keymapPath;      // config/keymap.txt
    std::string scriptConstantsPath; // config/script_constants.txt (level-script constants)
    // The tables read from the player's exe at start-up (Game/exe_tables.h), written to
    // <userDir>/exe_tables/: PlayDirectorSpeech lines, hunter lines, clip behaviours and blends.
    std::string directorSpeechPath;
    std::string hunterSpeechPath;
    std::string pedAnimsPath;
    std::string saveDir;         // The game's save folder: its save slots and Settings.dat (--save-dir, else gameSaveDir)
    std::string savePath;        // --save: restore this save when starting straight into a level
    std::string saveOutPath;     // where F5 writes (never the original save by default)
    std::string userDir;         // the engine's own settings (the launcher's video.cfg), with a trailing slash

    bool skipMenu = false;       // start straight into `level` (the original's SKIP_MENU setting)
    bool characterOnly = false;  // start straight into the character-only animation rig
    bool playCutscene = true;    // level mode: play the level's Bink intro first
    bool noMusic = false;        // --no-music: no level music
    bool unlockAll = false;      // --unlock-all: every scene open in SELECT SCENE
    bool movementEnabled = true; // level mode: the keys move the player (--no-movement freezes him)
    bool aiLog = false;
    std::vector<std::string> give; // debug: entityTypeData records to start with (--give Bag_(CT))
    std::vector<std::string> runScripts; // debug: <entity>:<handler> started with the level (--run-script)
    bool perfOverlay = false;    // show frame and GPU pass times from the start (F7 toggles)
    // Debug: start the player at this scene position (x, z) and heading (degrees) instead of the
    // level's start.
    std::optional<std::array<float32, 4>> playerAt;          // --player-at x,z[,yaw[,y]] (y: NaN = search from above)
    bool mouseLook = true;       // level mode: capture the mouse for looking around (off for scripted runs)
    // Level lighting: original (baked), mixed (baked + shadowed lamps), dynamic (no prelight),
    // midday (debug sun).
    Renderer::LightingMode lighting = Renderer::LightingMode::Mixed;
    bool validation = false;     // Vulkan validation layer
    // Start in the free-fly debug camera at this pose: x, y, z, yaw (deg), pitch (deg).
    std::optional<std::array<float32, 5>> debugCamera;
    uint32 width = 800u;
    uint32 height = 600u;
    // The launcher (Game/launcher.h): Show it before the menu (the original's way), Saved: start
    // with the last choice, Windowed: a --size window on the last chosen display. Starting
    // straight into a level (--skip-menu, --level, --character-only) means Windowed.
    enum class Launcher { Show, Saved, Windowed } launcher = Launcher::Show;
    [[nodiscard]] std::string videoChoicePath() const { return userDir + "video.cfg"; }
    // The SETTINGS menu's values (Game/settings.h), in the save folder's Settings.dat as the
    // original keeps them (Documents/the game's user-files folder/SaveGames).
    GameSettings settings;
    [[nodiscard]] std::string settingsPath() const { return saveDir + "/Settings.dat"; }

    [[nodiscard]] std::string levelDir() const { return gameRoot + "levels/" + level + "/"; }
    [[nodiscard]] std::string characterDffPath() const;
    [[nodiscard]] std::string animationsPath() const;
    [[nodiscard]] std::string cutscenePath() const { return levelDir() + level + ".bik"; }
    [[nodiscard]] std::string levelScriptPath() const { return levelDir() + "scripts/" + level + ".mls"; }
};

// Build-time defaults (see CMakeLists.txt). The save folder is set once the game's executable is
// read (gameSaveDir), unless --save-dir gives one.
AppConfig defaultConfig();

// <Documents>/<userFilesDir>/<saveGamesDir>: where the original game keeps its saves and settings
// (the folder names are the executable's, Game/exe_tables.h).
[[nodiscard]] std::string gameSaveDir(const std::string& userFilesDir, const std::string& saveGamesDir);

// Apply command-line overrides to `cfg`. Returns an error message for a bad argument, or
// std::nullopt on success. `--help` sets `helpRequested`.
std::optional<std::string> parseCommandLine(const std::vector<std::string>& args, AppConfig& cfg, bool& helpRequested);

[[nodiscard]] std::string commandLineHelp();

} // namespace Game
