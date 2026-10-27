// Game/level_setup.cpp - the tuning pak level render setup. See level_setup.h.
#include "Game/level_setup.h"

#include <array>
#include <sstream>

#include "Assets/ini_records.h"
#include "Assets/pak.h"

namespace Game {

namespace {

// Weather records in WEATHER_* order (the script constants: CLOUDY = 0 ... CLEAR = 5).
constexpr std::array<const char*, 6> kWeathers = {"CLOUDY", "WINDY", "RAINY", "THUNDERSTORM", "FOGGY", "CLEAR"};

bool avoided(const Assets::IniRecord& r) { return r.line("AVOID") != nullptr; }

// "2,2,5" (0..255) -> 0..1.
bool readColor(const Assets::IniRecord& r, const std::string& key, Math::Vector3& out)
{
    const auto red = r.number(key, 1), green = r.number(key, 2), blue = r.number(key, 3);
    if (!red || !green || !blue)
        return false;
    out = Math::Vector3{*red / 255.0f, *green / 255.0f, *blue / 255.0f};
    return true;
}

} // namespace

LevelSetup LevelSetup::read(const Assets::PakFile& pak, const std::string& level, int32 weather)
{
    LevelSetup s;
    if (const auto text = pak.file("levels/" + level + "/levelSetup.ini"))
    {
        const auto records = Assets::parseIniRecords(*text);
        if (const Assets::IniRecord* world = Assets::findRecord(records, "world1"))
        {
            if (const auto far = world->number("FAR_CLIP"))
                s.fogStart = s.fogEnd = *far; // no weather record: no fog, only the far clip
            readColor(*world, "FOG_COLOUR", s.skyColor);
        }
        const Assets::IniRecord* chosen = nullptr;
        if (weather >= 0 && weather < static_cast<int32>(kWeathers.size()))
            chosen = Assets::findRecord(records, kWeathers[static_cast<std::size_t>(weather)]);
        for (std::size_t i = 0; !chosen && weather < 0 && i < kWeathers.size(); ++i)
        {
            const Assets::IniRecord* r = Assets::findRecord(records, kWeathers[i]);
            if (r && !avoided(*r))
                chosen = r;
        }
        if (chosen && !avoided(*chosen))
        {
            s.weather = chosen->name;
            if (const auto far = chosen->number("FARCLIP"))
                s.fogEnd = *far;
            if (const auto start = chosen->number("FOGSTART"))
            {
                s.fogStart = *start;
                s.fog = s.fogStart < s.fogEnd;
            }
            readColor(*chosen, "SKY", s.skyColor);
        }
    }
    if (const auto text = pak.file("levels/" + level + "/entityTypeData.ini"))
        for (const Assets::IniRecord& r : Assets::parseIniRecords(*text))
            if (r.line("SHADOW"))
                s.shadowCasters.push_back(r.name);
    return s;
}

std::string LevelSetup::summary() const
{
    std::ostringstream o;
    o << "weather " << (weather.empty() ? "none" : weather) << ", fog ";
    if (fog)
        o << fogStart << ".." << fogEnd << " m";
    else
        o << "off (far clip " << fogEnd << " m)";
    o << ", sky (" << skyColor.x * 255.0f << ", " << skyColor.y * 255.0f << ", " << skyColor.z * 255.0f
      << "), shadow props " << shadowCasters.size();
    return o.str();
}

} // namespace Game
