// Game/launcher.h - the launcher window shown before the game starts (the original's start dialog).
//
// The exe dialog 104 (template in its resources; procedure 0x4bf3e0, shown by 0x4c0910
// with the cursor first put in the middle of the screen), 272 x 195 dialog units, MS Sans Serif 8:
//   MB_PICTURE        the banner bitmap, sunken, at (10, 9)
//   Play (IDOK)       (220, 8) 42 x 24, the default button
//   Quit (1002)       (220, 38) 42 x 24
//   "Video Settings"  group box (10, 100) 253 x 82, holding
//     "Display Adapter:" (17, 123) with its drop-down list 1000 at (77, 121) 172 wide
//     "Screen Mode:"     (17, 156) with its drop-down list 1001 at (77, 154) 172 wide
// Choosing an adapter refills the modes; Play saves the choice and starts the game, Quit (or
// closing it) ends the program. The banner is loaded from the user's exe at run time
// (resource MB_PICTURE); like the exe, the window has no icon. Lists: Game/video_modes.h.
#pragma once
#include "engine_types.h"

#include <optional>
#include <string>
#include <vector>

#include "Game/video_modes.h"

namespace Game {

// The displays, primary first (Direct3D 8's adapter order), each named by the GPU driving it.
std::vector<DisplayAdapter> enumerateAdapters();

// Shows the launcher (Windows; elsewhere it returns `saved`). nullopt: the player quit.
std::optional<VideoChoice> runLauncher(const std::string& gameExePath, const std::vector<DisplayAdapter>& adapters,
                                       const VideoChoice& saved);

} // namespace Game
