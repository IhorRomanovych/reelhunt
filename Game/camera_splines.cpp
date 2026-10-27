// Game/camera_splines.cpp - scripted camera paths. See camera_splines.h.
#include "Game/camera_splines.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include "Assets/binary_reader.h"
#include "Assets/ini_records.h"

namespace Game {

namespace {

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

Math::Vector3 catmullRom(const Math::Vector3& p0, const Math::Vector3& p1, const Math::Vector3& p2, const Math::Vector3& p3, float32 t)
{
    const float32 t2 = t * t, t3 = t2 * t;
    return (p1 * 2.0f + (p2 - p0) * t + (p0 * 2.0f - p1 * 5.0f + p2 * 4.0f - p3) * t2 + (p1 * 3.0f - p0 - p2 * 3.0f + p3) * t3) * 0.5f;
}

} // namespace

Math::Vector3 CameraSpline::at(float32 t) const
{
    if (points.empty())
        return {0.0f, 0.0f, 0.0f};
    if (points.size() == 1)
        return points.front();
    const std::size_t segments = points.size() - 1;
    const float32 f = std::clamp(t, 0.0f, 1.0f) * static_cast<float32>(segments);
    const std::size_t i = std::min(static_cast<std::size_t>(f), segments - 1);
    const float32 u = f - static_cast<float32>(i);
    const Math::Vector3& p1 = points[i];
    const Math::Vector3& p2 = points[i + 1];
    if (!smooth)
        return p1 + (p2 - p1) * u;
    const Math::Vector3& p0 = i > 0 ? points[i - 1] : p1;
    const Math::Vector3& p3 = i + 2 < points.size() ? points[i + 2] : p2;
    return catmullRom(p0, p1, p2, p3, u);
}

bool CameraSplines::load(const std::string& ini, const std::string& splPath)
{
    splines_.clear();
    auto bytes = Assets::readFileBytes(splPath);
    if (!bytes)
        return false;
    const std::size_t start = ini.find("#STARTCODE");
    const auto records = Assets::parseIniRecords(start == std::string::npos ? ini : ini.substr(start));

    std::size_t at = 0;
    for (const Assets::IniRecord& r : records)
    {
        if (at + 0x34 > bytes->size())
            break;
        uint32 count = 0;
        std::memcpy(&count, bytes->data() + at + 0x2c, 4);
        if (at + 0x34 + std::size_t{count} * 12u > bytes->size())
            break;
        CameraSpline s;
        for (uint32 i = 0; i < count; ++i)
        {
            float32 xyz[3];
            std::memcpy(xyz, bytes->data() + at + 0x34 + i * 12u, 12);
            s.points.push_back({xyz[0], xyz[1], xyz[2]});
        }
        at += 0x34 + std::size_t{count} * 12u;
        s.follow = r.number("ISFOLLOW").value_or(1.0f) != 0.0f;
        s.smooth = r.number("ISSMOOTH").value_or(1.0f) != 0.0f;
        s.duration = std::max(0.1f, r.number("DURATION").value_or(1.0f));
        splines_[upper(r.name)] = std::move(s);
    }
    return !splines_.empty();
}

const CameraSpline* CameraSplines::find(const std::string& name) const
{
    const auto it = splines_.find(upper(name));
    return it == splines_.end() ? nullptr : &it->second;
}

} // namespace Game
