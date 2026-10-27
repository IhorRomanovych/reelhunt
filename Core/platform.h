// Core/platform.h - the few things that differ between Windows and Linux: where the running
// executable lives, and how a path written for the game's (case-insensitive) Windows file layout
// is found on a case-sensitive file system.
//
// The original game data was authored on Windows: the code and the data's own text files refer to
// "levels/GLOBAL/CHARPAK", "JURY_TURF" and "jury_turf" alike. On Linux every path into the game
// data goes through resolvePath(), which keeps the path when it exists as written and otherwise
// matches each missing component case-insensitively against the directory's entries (cached).
#pragma once

#include <filesystem>
#include <string>

namespace Engine::Platform {

// The directory holding the running executable (empty if it cannot be found).
[[nodiscard]] std::filesystem::path executableDir();

// `path` as it exists on disk: unchanged when it exists (and always on Windows); else each
// component matched case-insensitively. A component with no match leaves the rest as written.
[[nodiscard]] std::string resolvePath(const std::string& path);

// Whether `dir` looks like a game install: it holds levels/, audio/ and pictures/ (any case).
[[nodiscard]] bool isGameRoot(const std::filesystem::path& dir);

} // namespace Engine::Platform
