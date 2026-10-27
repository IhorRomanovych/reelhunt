// Game/exe_tables.h - the lookup tables the engine reads out of the player's own exe.
//
// A few tables the game needs live in its executable, not in its data files: the director's and
// the hunters' speech tables, the ped clip names and behaviours, the default key bindings, and
// the names of some of its own files (save slots, the user folder, the tuning archive, the text
// font's texture). They are not shipped with the engine. At start-up they are read from the
// game's executable in the game folder (the PC 1.0 build, found by its content) and the text
// tables are written to the user folder, in the formats Game/director_speech.h,
// Game/hunter_speech.h and Game/ped_anims.h load.
//
// Sample numbers in the speech tables index the SFX bank in sfx.lst order (its "-- NAME" lines),
// so the text tables name samples rather than numbers.
#pragma once
#include "engine_types.h"

#include <array>
#include <expected>
#include <string>

namespace Game {

struct ExeTables {
    std::string exePath;        // the executable they were read from
    std::string directorSpeech; // Game/director_speech.h's text format
    std::string hunterSpeech;   // Game/hunter_speech.h's
    std::string pedAnims;       // Game/ped_anims.h's
    std::array<uint8, 0x260> defaultBindings{}; // Settings.dat's two key tables, as the exe resets them
    std::array<std::string, 8> saveNames;       // the save slots' file names
    std::string userFilesDir;                   // <Documents>/<this>: the game's saves and settings
    std::string saveGamesDir;                   // its sub-folder of saves
    std::string tuningPak;                      // the tuning archive, relative to the game root
    std::string fontAtlas;                      // The game text font's texture in frontend_pc.txd
};

// Reads the tables from `exePath`; `sfxLstPath` is the game's audio/PC/SFX/sfx.lst. The error
// says what is wrong (missing file, another build of the exe).
std::expected<ExeTables, std::string> readExeTables(const std::string& exePath, const std::string& sfxLstPath);

// The game's executable in `gameRoot` (with a trailing slash): the first .exe there whose tables
// read, whatever its name.
std::expected<ExeTables, std::string> findGameExe(const std::string& gameRoot);

// The running game's tables, set once at start-up (all empty until then).
const ExeTables& gameExe();
void setGameExe(ExeTables tables);

// Writes the text tables to `dir` (created): director_speech.txt, hunter_speech.txt, ped_anims.txt.
bool writeExeTables(const ExeTables& tables, const std::string& dir);

} // namespace Game
