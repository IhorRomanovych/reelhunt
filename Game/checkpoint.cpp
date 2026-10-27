// Game/checkpoint.cpp - the level's live state as a save's entity block (see checkpoint.h).
#include "Game/checkpoint.h"
#include "Core/platform.h"

#include "Game/level_frame.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>

namespace Game::Checkpoint {

Math::Vector4 instRotation(const std::vector<float>& plane)
{
    if (plane.size() < 4)
        return Math::Vector4{0.0f, 0.0f, 0.0f, 1.0f};
    return Math::Vector4{plane[0], plane[2], -plane[1], plane[3]};
}

std::vector<float> saveRotation(const Math::Vector4& q)
{
    // 0x43a2c0 makes the quaternion from the rotation matrix, which fixes its sign: with a
    // positive trace (|w| > 0.5) w is positive, else the largest of x, y, z is.
    std::vector<float> s = {q.x, -q.z, q.y, q.w};
    float sign = 1.0f;
    if (4.0f * s[3] * s[3] - 1.0f > 0.0f)
        sign = s[3] < 0.0f ? -1.0f : 1.0f;
    else
    {
        std::size_t big = 0;
        for (std::size_t i = 1; i < 3; ++i)
            if (std::fabs(s[i]) > std::fabs(s[big]))
                big = i;
        sign = s[big] < 0.0f ? -1.0f : 1.0f;
    }
    for (float& c : s)
        c *= sign;
    return s;
}

Math::Vector3 instPosition(const LevelFrame& frame, const Math::Vector3& scene)
{
    const Math::Vector3 p = scene * (1.0f / frame.scale) + frame.center;
    return Math::Vector3{p.x, -p.z, p.y};
}

Math::Vector4 instYawRotation(float32 placementYaw)
{
    // placementYaw = -2 atan2(z, w) for a turn about the .inst frame's +Z.
    const float32 half = -placementYaw * 0.5f;
    return Math::Vector4{0.0f, 0.0f, std::sin(half), std::cos(half)};
}

std::vector<std::uint8_t> globalScriptData(const std::string& sccPath)
{
    std::vector<std::uint8_t> out(SaveLayout::buffer4, 0);
    std::ifstream in(Engine::Platform::resolvePath(sccPath), std::ios::binary);
    const std::vector<std::uint8_t> file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    for (std::size_t i = 0; i + 8 <= file.size(); ++i)
        if (std::memcmp(file.data() + i, "DATA", 4) == 0)
        {
            std::uint32_t size = 0;
            std::memcpy(&size, file.data() + i + 4, 4);
            for (std::size_t k = 0; k < size && k < out.size() && i + 8 + k < file.size(); ++k)
                out[k] = file[i + 8 + k] == 0xda ? 0 : file[i + 8 + k];
            break;
        }
    return out;
}

SaveEntityRecord nulRecord()
{
    SaveEntityRecord r;
    r.name = "NUL";
    return r;
}

std::uint32_t word(const SaveEntityRecord& r, std::size_t i)
{
    std::uint32_t v = 0;
    if (r.classData.size() >= (i + 1) * 4)
        std::memcpy(&v, r.classData.data() + i * 4, 4);
    return v;
}

void setWord(SaveEntityRecord& r, std::size_t i, std::uint32_t v)
{
    if (r.classData.size() < (i + 1) * 4)
        r.classData.resize((i + 1) * 4, 0);
    std::memcpy(r.classData.data() + i * 4, &v, 4);
}

SaveEntityRecord record(const Assets::InstPlacement& p)
{
    SaveEntityRecord r;
    r.name = p.record;
    r.instance = p.internalName;
    r.pos[0] = p.position.x;
    r.pos[1] = p.position.y;
    r.pos[2] = p.position.z;
    r.plane = saveRotation(p.rotation);
    for (std::size_t i = 0; i < p.params.size(); ++i)
        setWord(r, i, static_cast<std::uint32_t>(p.params[i]));
    // The words the .inst packs with an offset (steps of 0x1c00) keep only their low bits in
    // a save (Jury_Turf's lamp: 0x11801 -> 1); the light's second word is one less.
    constexpr std::uint32_t kLow = 0x3ff;
    if (p.entityClass == "CameraData")
        setWord(r, 1, word(r, 1) & kLow);
    else if (p.entityClass == "Hunter_Inst" && p.params.size() > 8)
        setWord(r, 8, word(r, 8) & kLow);
    else if (p.entityClass == "Door_Inst" && p.params.size() > 2)
        setWord(r, 2, word(r, 2) & kLow);
    else if (p.entityClass == "Light_Inst" && p.params.size() > 12)
    {
        setWord(r, 1, word(r, 1) - 1);
        for (std::size_t i = 6; i <= 12; ++i)
            setWord(r, i, word(r, i) & kLow);
    }
    return r;
}

void apply(std::vector<Assets::InstPlacement>& placements, const std::vector<SaveEntityRecord>& records)
{
    std::vector<Assets::InstPlacement> kept;
    kept.reserve(placements.size());
    for (Assets::InstPlacement& p : placements)
    {
        if (p.index < 0 || static_cast<std::size_t>(p.index) >= records.size())
        {
            kept.push_back(std::move(p));
            continue;
        }
        const SaveEntityRecord& r = records[static_cast<std::size_t>(p.index)];
        if (r.name == "NUL" || r.instance != p.internalName)
            continue; // gone (or not this placement's record: the level changed)
        p.position = Math::Vector3{r.pos[0], r.pos[1], r.pos[2]};
        p.rotation = instRotation(r.plane);
        // The class data over the parameters, but for the words record() writes from the live
        // entity (the .inst's own values stay, so a checkpoint writes back as it was read).
        auto liveWord = [&](std::size_t i) {
            if (p.entityClass == "CameraData")
                return i == 1;
            if (p.entityClass == "Hunter_Inst")
                return i == 8;
            if (p.entityClass == "Door_Inst")
                return i == 2;
            if (p.entityClass == "Light_Inst")
                return i == 1 || (i >= 6 && i <= 12);
            return false;
        };
        for (std::size_t i = 0; i < r.classData.size() / 4; ++i)
        {
            const std::uint32_t w = word(r, i);
            if (i >= p.params.size())
                p.params.push_back(static_cast<int32>(w));
            else if (!liveWord(i))
                p.params[i] = static_cast<int32>(w);
        }
        kept.push_back(std::move(p));
    }
    placements = std::move(kept);
}

} // namespace Game::Checkpoint
