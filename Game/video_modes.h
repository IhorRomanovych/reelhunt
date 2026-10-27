// Game/video_modes.h - the launcher's screen modes and the saved choice (Game/launcher.h).
//
// The exe's launcher (dialog 104, procedure 0x4bf3e0, set-up 0x4bef40, mode list 0x4bee40)
// lists the RenderWare subsystems (one per display adapter, at most 16) and, for the chosen
// one, every video mode that is fullscreen (rwVIDEOMODEEXCLUSIVE) and at least 640 x 480, as
// "%lu x %lu x %lu ( Fullscreen )" (width, height, depth). Its "%lu bit color windowed" entry is
// never reached: windowed modes are filtered out.
// After Play (0x4c0910) a fullscreen mode gets a refresh rate from the adapter's own modes of
// that size and format (0x4c0830): the default rate if one is listed, else the lowest of at
// least 60 Hz.
// The exe keeps Device (adapter), Mode (RenderWare mode index) and Index (list row) in
// HKCU\SOFTWARE\Rockstar Games\the game\Video. Ours keeps the adapter, the row and the mode's
// size in a file of its own: a real install reads that key, and our rows are not RW indices.
#pragma once
#include "engine_types.h"

#include <optional>
#include <string>
#include <vector>

namespace Game {

struct ScreenMode {
    uint32 width = 0u, height = 0u, depth = 32u;
    float32 refresh = 0.0f; // Hz, 0 = the display's default

    [[nodiscard]] bool sameSize(const ScreenMode& o) const noexcept
    {
        return width == o.width && height == o.height && depth == o.depth;
    }
};

// One display adapter as the launcher lists it (a display and the GPU driving it).
struct DisplayAdapter {
    uint32 id = 0u;                 // SDL display id
    std::string name;               // the GPU's name, as Direct3D 8 named the adapter
    std::string monitor;            // the display's own name
    std::vector<ScreenMode> modes;  // every fullscreen mode, with its refresh rates
    ScreenMode desktop;
};

// The launcher's rows: fullscreen modes of at least 640 x 480, one per size and depth, smallest
// first (the adapter's enumeration order).
std::vector<ScreenMode> launcherModes(const std::vector<ScreenMode>& all);
// "800 x 600 x 32 ( Fullscreen )".
std::string modeLabel(const ScreenMode& m);
// The refresh rate for `m` (0x4c0830): 0 if a default-rate mode is listed, else the lowest of at
// least 60 Hz, else 0.
float32 launcherRefresh(const std::vector<ScreenMode>& all, const ScreenMode& m);
// The Display Adapter list: each adapter's name; adapters that share a name (one GPU driving
// several displays: the original lists them all alike) get their display's name after it (ours,
// so the screen can be told apart).
std::vector<std::string> adapterLabels(const std::vector<DisplayAdapter>& adapters);

// The launcher's choice.
struct VideoChoice {
    int32 device = 0;  // row in the Display Adapter list
    int32 index = -1;  // row in the Screen Mode list
    ScreenMode mode{800u, 600u, 32u, 0.0f};
};

// The row to select for `saved` in `rows`: its own row if it still shows that mode, else the row
// of that mode, else 800 x 600 x 32 (the exe's preferred mode, 0x4bf3e0's 0x3ee case), else 0.
int32 launcherModeRow(const std::vector<ScreenMode>& rows, const VideoChoice& saved);

std::optional<VideoChoice> loadVideoChoice(const std::string& path);
bool saveVideoChoice(const std::string& path, const VideoChoice& c);

} // namespace Game
