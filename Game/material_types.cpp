// Game/material_types.cpp - world texture -> material type. See material_types.h.
#include "Game/material_types.h"
#include "Core/platform.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace Game {

namespace {

constexpr std::size_t kNameBytes = 200;

// 0x7ac544: the hit effect of each type (0 none).
constexpr std::array<uint8, MaterialTypes::kTypes> kHitEffect{
    0, 3, 2, 5, 8, 0, 10, 4, 0, 0, 5, 4, 0, 0, 5, 0, 0, 0, 10, 3, 2, 5, 9, 9, 6, 0, 0, 0, 0, 2, 3, 4,
    0, 2, 2, 2, 2, 2, 2, 4, 4, 4, 5, 4, 4, 1, 0, 7, 3, 4, 0, 0, 0, 11, 9, 7, 4, 5, 6, 0, 0, 0, 0};

// 0x7ac448: the bullet-hole decal of each type; 0x7ac2cc and 0x7ac389: the flags that allow it.
constexpr std::array<uint8, MaterialTypes::kTypes> kBulletHole{
    8, 10, 11, 12, 8, 8, 13, 14, 8, 8, 12, 12, 8, 8, 12, 8, 8, 15, 13, 16, 11, 17, 8, 8, 8, 8, 19, 8, 8, 8, 20, 8,
    8, 11, 11, 11, 11, 11, 11, 12, 12, 12, 17, 12, 17, 21, 22, 23, 24, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 15, 8, 8};
constexpr std::array<uint8, MaterialTypes::kTypes> kHoleFlagA{
    1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 0, 1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0};
constexpr std::array<uint8, MaterialTypes::kTypes> kHoleFlagB{
    1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, 1, 0, 0};

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

} // namespace

bool MaterialTypes::load(const std::string& path)
{
    std::ifstream f(Engine::Platform::resolvePath(path), std::ios::binary);
    if (!f)
        return false;
    const std::vector<char> d{std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
    auto word = [&](std::size_t at) {
        int32 v = 0;
        if (at + 4 <= d.size())
            std::memcpy(&v, d.data() + at, 4);
        return v;
    };
    const int32 types = word(0);
    if (types <= 0 || types > kTypes)
        return false;
    std::size_t at = 4 + 4 * static_cast<std::size_t>(types);
    for (int32 t = 0; t < types; ++t)
        for (int32 n = 0, count = word(4 + 4 * static_cast<std::size_t>(t)); n < count; ++n, at += kNameBytes)
        {
            if (at + kNameBytes > d.size())
                return false;
            const char* name = d.data() + at;
            types_.emplace(lower(std::string(name, strnlen(name, kNameBytes))), static_cast<uint8>(t));
        }
    return true;
}

uint8 MaterialTypes::typeOf(const std::string& texture) const
{
    const auto it = types_.find(lower(texture));
    return it == types_.end() ? kNone : it->second;
}

int32 MaterialTypes::bulletHole(uint8 type) noexcept
{
    if (type >= kTypes || !kHoleFlagA[type] || !kHoleFlagB[type])
        return -1;
    return kBulletHole[type];
}

float32 MaterialTypes::soundThrough(uint8 type) noexcept
{
    static constexpr float32 kThrough[kTypes] = {
        0.75f, 0.5f, 0.5f, 0.5f, 0.75f, 0.5f, 0.5f, 0.5f, 1.0f,  1.0f,  0.5f,  0.9f,  0.9f,  1.0f,  0.9f,  0.1f,
        1.0f,  0.9f, 0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f, 0.9f,  0.0f,  0.5f,  0.9f,  1.0f,  0.9f,  1.0f,  1.0f,
        1.0f,  0.5f, 0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f, 0.5f,  0.9f,  0.5f,  0.9f,  0.5f,  0.75f, 0.75f, 0.75f,
        0.5f,  0.9f, 1.0f, 1.0f, 1.0f,  0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 1.0f, 0.9f, 1.0f, 1.0f};
    return type < kTypes ? kThrough[type] : 1.0f;
}

bool MaterialTypes::stopsSound(uint8 type) noexcept
{
    static constexpr uint8 kStops[kTypes] = {1, 1, 1, 1, 0, 1, 1, 1, 0, 0, 1, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1,
                                             1, 1, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1,
                                             1, 1, 1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0};
    return type < kTypes && kStops[type] != 0;
}

float32 MaterialTypes::shellLevel(uint8 type) noexcept
{
    static constexpr float32 kLevel[kTypes] = {
        0.9f, 0.9f, 0.75f, 1.0f, 0.5f, 0.6f, 0.9f, 0.8f, 0.5f, 0.3f, 0.9f, 0.9f, 0.9f, 1.0f, 0.9f, 0.3f,
        1.0f, 0.9f, 0.9f, 0.6f, 0.8f, 1.0f, 0.4f, 0.2f, 0.0f, 0.0f, 0.5f, 0.6f, 1.0f, 0.8f, 1.0f, 1.0f,
        1.0f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 0.75f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.3f, 0.4f, 0.4f,
        0.6f, 0.75f, 0.75f, 0.85f, 0.9f, 0.6f, 0.3f, 0.7f, 0.9f, 0.9f, 0.3f, 0.5f, 0.9f, 0.9f, 1.0f};
    return type < kTypes ? kLevel[type] : 1.0f;
}

MaterialTypes::Steps MaterialTypes::footsteps(uint8 type) noexcept
{
    // By type: 0 concrete, 1 gravel, 2 water, 3 grass, 4 mud, 5 wood, 6 metal, 7 tile, 8 wire
    // mesh, 9 guts, 10 none (sample 14099, the bank's end).
    static constexpr Steps kRanges[] = {{79, 83}, {89, 93}, {94, 98}, {84, 88}, {109, 112}, {99, 103},
                                        {104, 108}, {113, 116}, {26, 28}, {230, 234}, {-1, -1}};
    static constexpr uint8 kOf[kTypes] = {0, 0, 5, 6, 0, 1, 7, 6, 8, 0, 6, 6, 0, 0, 6, 2, 10, 7, 7, 0, 5,
                                          6, 4, 4, 3, 2, 4, 0, 0, 0, 0, 0, 0, 5, 5, 5, 5, 5, 5, 6, 6, 6,
                                          6, 6, 6, 3, 3, 0, 0, 6, 5, 6, 0, 9, 3, 10, 6, 8, 10, 4, 10, 8, 10};
    if (type >= kTypes)
        return kRanges[0];
    if (type == 56)
        return {104, 104}; // (METAL_1 alone)
    return kRanges[kOf[type]];
}

MaterialTypes::Ricochet MaterialTypes::ricochet(uint8 type) noexcept
{
    switch (type)
    {
    case 0x01: case 0x06: case 0x12: case 0x13: case 0x30: case 0x34:
        return {"SFX_WEAPON_BULLET_RICOCHET_STONE", 1.0f};
    case 0x02: case 0x14: case 0x1d: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x32:
        return {"SFX_WEAPON_BULLET_RICOCHET_WOOD", 1.0f};
    case 0x03: case 0x07: case 0x0a: case 0x27: case 0x28: case 0x29: case 0x31:
        return {"SFX_WEAPON_BULLET_IMPACT_METAL", 1.0f};
    case 0x0b: case 0x0e: case 0x15: case 0x2a: case 0x2b: case 0x2c: case 0x33:
        return {"SFX_WEAPON_BULLET_RICOCHET_METAL", 1.0f};
    default:
        return {"SFX_WEAPON_BULLET_RICOCHET_WOOD", 0.672f};
    }
}

const char* MaterialTypes::hitEffect(uint8 type) noexcept
{
    static constexpr const char* kNames[] = {nullptr,   nullptr,   "FXBWOOD", "FXBSTON", "FXBTMET2", "FXBTMET",
                                             "FXBTURF", "FXBPLAS", "FXBPAPE", "FXBMUD",  "FXBCERM",  "FXP003"};
    return type < kTypes ? kNames[kHitEffect[type]] : nullptr;
}

} // namespace Game
