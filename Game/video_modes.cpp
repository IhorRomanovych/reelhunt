// Game/video_modes.cpp - the launcher's screen modes and the saved choice. See video_modes.h.
#include "Game/video_modes.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace Game {

std::vector<ScreenMode> launcherModes(const std::vector<ScreenMode>& all)
{
    std::vector<ScreenMode> rows;
    for (const ScreenMode& m : all)
    {
        // 0x4bee40: width > 0x27f and height > 0x1df.
        if (m.width < 640u || m.height < 480u)
            continue;
        if (std::none_of(rows.begin(), rows.end(), [&](const ScreenMode& r) { return r.sameSize(m); }))
            rows.push_back({m.width, m.height, m.depth, 0.0f});
    }
    std::sort(rows.begin(), rows.end(), [](const ScreenMode& a, const ScreenMode& b) {
        if (a.depth != b.depth)
            return a.depth < b.depth;
        if (a.width != b.width)
            return a.width < b.width;
        return a.height < b.height;
    });
    return rows;
}

std::string modeLabel(const ScreenMode& m)
{
    char buf[100];
    std::snprintf(buf, sizeof(buf), "%lu x %lu x %lu ( Fullscreen )", static_cast<unsigned long>(m.width),
                  static_cast<unsigned long>(m.height), static_cast<unsigned long>(m.depth));
    return buf;
}

float32 launcherRefresh(const std::vector<ScreenMode>& all, const ScreenMode& m)
{
    float32 best = 0.0f;
    for (const ScreenMode& a : all)
    {
        if (!a.sameSize(m))
            continue;
        if (a.refresh <= 0.0f)
            return 0.0f;
        if (a.refresh > 59.0f && (best == 0.0f || a.refresh < best))
            best = a.refresh;
    }
    return best;
}

std::vector<std::string> adapterLabels(const std::vector<DisplayAdapter>& adapters)
{
    std::map<std::string, int> uses;
    for (const DisplayAdapter& a : adapters)
        ++uses[a.name];
    std::vector<std::string> out;
    for (const DisplayAdapter& a : adapters)
        out.push_back(uses[a.name] > 1 && !a.monitor.empty() ? a.name + " - " + a.monitor : a.name);
    return out;
}

int32 launcherModeRow(const std::vector<ScreenMode>& rows, const VideoChoice& saved)
{
    const auto n = static_cast<int32>(rows.size());
    if (saved.index >= 0 && saved.index < n && rows[static_cast<std::size_t>(saved.index)].sameSize(saved.mode))
        return saved.index;
    for (int32 i = 0; i < n; ++i)
        if (rows[static_cast<std::size_t>(i)].sameSize(saved.mode))
            return i;
    for (int32 i = 0; i < n; ++i)
        if (rows[static_cast<std::size_t>(i)].sameSize({800u, 600u, 32u, 0.0f}))
            return i;
    return 0;
}

std::optional<VideoChoice> loadVideoChoice(const std::string& path)
{
    std::ifstream f(path);
    if (!f)
        return std::nullopt;
    VideoChoice c;
    bool any = false;
    for (std::string line; std::getline(f, line);)
    {
        std::istringstream in(line);
        std::string key;
        if (!(in >> key) || key[0] == '#')
            continue;
        if (key == "device")
            any = static_cast<bool>(in >> c.device) || any;
        else if (key == "index")
            any = static_cast<bool>(in >> c.index) || any;
        else if (key == "mode")
            any = static_cast<bool>(in >> c.mode.width >> c.mode.height >> c.mode.depth) || any;
    }
    if (!any)
        return std::nullopt;
    return c;
}

bool saveVideoChoice(const std::string& path, const VideoChoice& c)
{
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f)
        return false;
    f << "# The game launcher: the display adapter and screen mode (Game/video_modes.h)\n"
      << "device " << c.device << "\n"
      << "index " << c.index << "\n"
      << "mode " << c.mode.width << " " << c.mode.height << " " << c.mode.depth << "\n";
    return static_cast<bool>(f);
}

} // namespace Game
