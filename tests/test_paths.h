// tests/test_paths.h - where the tests find game data, reference fixtures and scratch space.
//
// Defaults come from CMake (tests/CMakeLists.txt); each can be overridden by an environment
// variable so the suite runs on any machine:
//   REELHUNT_GAME_ROOT      game data root (levels/, audio/, ...)
//   REELHUNT_TEST_FIXTURES  reference outputs (FFmpeg decodes etc.; see docs/BUILDING.md)
//   REELHUNT_SAVE_DIR       directory holding the original save slot 1
#pragma once
#include "Core/platform.h"
#include "Game/app_config.h"
#include "Game/exe_tables.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace TestPaths {

inline std::string fromEnv(const char* var, const std::string& fallback) {
    const char* v = std::getenv(var);
    return (v && *v) ? std::string(v) : fallback;
}

inline std::string withSlash(std::string p) {
    if (!p.empty() && p.back() != '/' && p.back() != '\\')
        p += '/';
    return p;
}

// Game data root, with a trailing slash.
inline std::string gameRoot() { return withSlash(fromEnv("REELHUNT_GAME_ROOT", REELHUNT_TEST_GAME_ROOT)); }
inline std::string level(const std::string& name) { return gameRoot() + "levels/" + name + "/"; }
inline std::string charpak() { return gameRoot() + "levels/GLOBAL/CHARPAK/"; }

// Engine source tree (config files).
inline std::string sourceDir() { return withSlash(REELHUNT_SOURCE_DIR); }

// Reference fixtures (generated once with FFmpeg etc.), with a trailing slash.
inline std::string fixtures() { return withSlash(fromEnv("REELHUNT_TEST_FIXTURES", REELHUNT_TEST_FIXTURES_DIR)); }

// A writable scratch directory for test outputs (created on first use), with a trailing slash.
inline std::string scratch() {
    std::error_code ec;
    const auto dir = std::filesystem::temp_directory_path(ec) / "reelhunt_engine_tests";
    std::filesystem::create_directories(dir, ec);
    return withSlash(dir.string());
}

// ctest treats this exit code as "skipped" (SKIP_RETURN_CODE in tests/CMakeLists.txt).
inline constexpr int kSkip = 77;

// Whether a game install is at gameRoot(). Tests that read the game data skip without one, so the
// suite runs (data-free parts only) on a machine or CI runner that has no copy of the game.
// The game's executable is read too (exeTables): its tables and file names are part of the data.
inline const Game::ExeTables* exeTables();
inline bool haveGameData() { return Engine::Platform::isGameRoot(gameRoot()) && exeTables() != nullptr; }

// The tables the engine reads from the game's executable (Game/exe_tables.h), read once and made
// the running game's (Game::gameExe): nullptr without a game install. exeTablesDir() writes them to the scratch folder: that folder, or "" without one.
inline const Game::ExeTables* exeTables() {
    static const std::optional<Game::ExeTables> tables = []() -> std::optional<Game::ExeTables> {
        auto t = Game::findGameExe(gameRoot());
        if (!t)
            return std::nullopt;
        Game::setGameExe(*t); // the names the engine reads from it (save slots, the font atlas, ...)
        return std::optional<Game::ExeTables>(std::move(*t));
    }();
    return tables ? &*tables : nullptr;
}
inline std::string exeTablesDir() {
    static const std::string dir = [] {
        const std::string d = scratch() + "exe_tables/";
        return exeTables() && Game::writeExeTables(*exeTables(), d) ? d : std::string();
    }();
    return dir;
}

// The original game's save directory (named by its executable), with a trailing slash.
inline std::string saveDir() {
    std::string def = ".";
    if (const Game::ExeTables* t = exeTables())
        def = Game::gameSaveDir(t->userFilesDir, t->saveGamesDir);
    return withSlash(fromEnv("REELHUNT_SAVE_DIR", def));
}

inline int skipNoGameData() {
    std::printf("SKIP: no game data at %s (set REELHUNT_GAME_ROOT)\n", gameRoot().c_str());
    return kSkip;
}

} // namespace TestPaths
