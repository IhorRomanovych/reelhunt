// Game/level_setup.h - a level's render setup from the tuning pak: fog, far clip and sky colour
// (levels/<level>/levelSetup.ini) and the props that cast real-time shadows (entityTypeData.ini).
//
// levelSetup.ini holds a "world1" record (FOG_COLOUR, FOG_TYPE LINEAR|EXP|EXP2, FOG_DENSITY,
// NEAR_CLIP, FAR_CLIP, AMBIENT; parsed by the exe 0x476fa1..0x477121, fog type at +4,
// density +8, near +0xc, far +0x10) followed by one record per weather, in the order of the
// script constants WEATHER_CLOUDY (0), WINDY, RAINY, THUNDERSTORM, FOGGY, CLEAR (5). A weather
// record is AVOID (the level never uses it) or FARCLIP / FOGSTART / SKY / CLOUDS. Every level
// uses LINEAR fog: it fades from FOGSTART to the far clip, into the sky colour (where a level
// sets FOG_COLOUR it equals its weather's SKY, e.g. Zoo 17,17,17).
#pragma once
#include "engine_types.h"

#include <string>
#include <vector>

#include "Math/vector.h"

namespace Assets {
class PakFile;
}

namespace Game {

struct LevelSetup {
    std::string weather;                          // the weather record in use ("CLEAR", ...)
    bool fog = false;                             // linear fog from fogStart to fogEnd
    float32 fogStart = 100.0f;                    // metres from the camera
    float32 fogEnd = 120.0f;                      // the far clip
    Math::Vector3 skyColor{0.008f, 0.008f, 0.02f}; // background and fog colour (0..1)

    // entityTypeData.ini records flagged SHADOW (burning drums, bins, trolleys): props the
    // original gives a real-time shadow, as it does characters.
    std::vector<std::string> shadowCasters;

    // Reads levels/<level>/levelSetup.ini and entityTypeData.ini. `weather` picks a weather
    // record by its WEATHER_* index; -1 takes the first one the level does not AVOID (the
    // scripts may force another with ForceWeatherType).
    static LevelSetup read(const Assets::PakFile& pak, const std::string& level, int32 weather = -1);

    // One line for the log.
    [[nodiscard]] std::string summary() const;
};

} // namespace Game
