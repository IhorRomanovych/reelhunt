// Game/settings.h - the player's settings (the SETTINGS pages) and Settings.dat.
//
// The exe keeps them in a block of 24 ints at 0x7d6af0, set to its defaults by 0x604c70,
// and stores them with the key bindings in the game's save folder (<Documents>/<its user-files folder>/SaveGames)/Settings.dat:
// 0x130 bytes of bindings (0x710bf4), the 0x60-byte block, 0x130 more bytes of bindings
// (0x710d24). It reads the file at start (0x605e50) and writes it when the game quits (0x606130,
// from the Quit page). Ours reads and writes the same file the same way; the bindings pass
// through untouched until controls can be remapped.
// What each value does (traced consumers):
//   language      the text files: 0 pc_game.gxt / pc_<level>.gxt, 1 _ger / _g, 2 _fre / _f,
//                 3 _ita / _i, 4 _spa / _s (0x493230; the Languages page lists ENG FRA ITA GER ESP)
//   subtitles     a video's <name>_n.SUB subtitles (0x4c0c60)
//   sfx, music    0..15: the effects / music buses at v / 15 x 127 (0x44b0d0); videos play at
//                 the effects volume (BinkSetVolume, v / 15 x 32767)
//   noiseFx       the static-noise overlay (0x5f7a50); cameraFx the camcorder effects of the
//                 executions and a camera filter (0x58dbc0, 0x5e8a70)
//   ratio         0 4:3, 1 widescreen: the camera's view window + 0.04 wide, - 0.06 high (0x476aa0)
//   gameDetail    0..3: a detail level (0x59e190: 0-1 -> 0, 2 -> 1, 3 -> 2) and a flag (> 0)
//   shadowDetail  0..3: shadows on from 2, texture 512 (odd) else 256 (0x5c3510)
//   invertY, mouseX, mouseY  the look: a delta times v / 19 x 0.5 + 0.08 (0x491db0)
//   brightness    0..20: the gamma ramp, exponent 1.2 - v / 20 (0x5daa50 / 0x5db0d0)
//   difficulty    0 Fetish, 1 Hardcore
#pragma once
#include "engine_types.h"

#include <array>
#include <string>

namespace Game {

struct GameSettings {
    // The block at 0x7d6af0, in order (Settings.dat + 0x130).
    int32 language = 0;
    int32 subtitles = 1;
    int32 sfxVolume = 14;
    int32 musicVolume = 14;
    int32 unknown0 = 12, unknown1 = 1, unknown2 = 3, unknown3 = 3, unknown4 = 3; // headset / speakers (PS2 pages)
    int32 noiseFx = 1;
    int32 ratio = 0;
    int32 unknown5 = 0, unknown6 = 0, unknown7 = 0;
    int32 gameDetail = 3;
    int32 shadowDetail = 3;
    int32 cameraFx = 1;
    int32 unknown8 = 0;
    int32 invertY = 0;
    int32 vibration = 1;
    int32 mouseX = 10;
    int32 mouseY = 10;
    int32 brightness = 6;
    int32 difficulty = 0;

    // Steps of each slider (0x5d52ca..0x5d550a).
    static constexpr int32 kVolumeSteps = 16;
    static constexpr int32 kDetailSteps = 4;
    static constexpr int32 kMouseSteps = 20;
    static constexpr int32 kBrightnessSteps = 21;

    [[nodiscard]] float32 sfxGain() const noexcept { return static_cast<float32>(sfxVolume) / (kVolumeSteps - 1); }
    [[nodiscard]] float32 musicGain() const noexcept { return static_cast<float32>(musicVolume) / (kVolumeSteps - 1); }
    // The look's scale for a mouse setting (0x491db0).
    [[nodiscard]] static float32 mouseScale(int32 v) noexcept
    {
        return static_cast<float32>(v) / (kMouseSteps - 1) * 0.5f + 0.08f;
    }
    // The gamma exponent (0x5db0d0: (1 - b) + 0.2 with b = brightness / 20).
    [[nodiscard]] float32 gammaExponent() const noexcept
    {
        return 1.2f - static_cast<float32>(brightness) / (kBrightnessSteps - 1);
    }
    // "", "_ger", "_fre", "_ita", "_spa" (pc_game) and "", "_g", "_f", "_i", "_s" (levels).
    [[nodiscard]] std::string globalTextSuffix() const;
    [[nodiscard]] std::string levelTextSuffix() const;
};
static_assert(sizeof(GameSettings) == 0x60, "the block at 0x7d6af0");

// Settings.dat: bindings, the settings block, more bindings (704 bytes).
struct SettingsFile {
    std::array<uint8, 0x130> bindingsA{};
    GameSettings settings;
    std::array<uint8, 0x130> bindingsB{};

    // The exe's defaults (0x604c70). The key bindings are the exe's own table (0x710994 / 0x710ac4),
    // read from the game's executable at start-up (Game/exe_tables.h).
    static SettingsFile defaults();
    [[nodiscard]] bool load(const std::string& path);
    [[nodiscard]] bool save(const std::string& path) const;
};

} // namespace Game
