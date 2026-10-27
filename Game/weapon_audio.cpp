// Game/weapon_audio.cpp - see weapon_audio.h.
#include "Game/weapon_audio.h"

namespace Game::WeaponAudio {

namespace {

// Samples (sfx.lst).
constexpr int32 kSwingLo = 0x89, kSwing = 0x8a, kSwingHi = 0x8b;
constexpr int32 kNailL = 0xb7, kNailR = 0xb8;
constexpr int32 kGlockL = 0xb9, kGlockR = 0xba, kGlockTailL = 0xbb, kGlockTailR = 0xbc;
constexpr int32 kShotgunL = 0xbd, kShotgunR = 0xbe;
constexpr int32 kTranqL = 0xbf, kTranqR = 0xc0;
constexpr int32 kSniperL = 0xc1, kSniperR = 0xc2;
constexpr int32 kUziL = 0xc3, kUziR = 0xc4, kUziShortL = 0xc5, kUziShortR = 0xc6;
constexpr int32 kColtL = 0xc7, kColtR = 0xc8, kColtShortL = 0xc9, kColtShortR = 0xca;
constexpr int32 kLongTailL = 0xcb, kLongTailR = 0xcc;
constexpr int32 kPanLeft = 270, kPanRight = 90;

// A gun with a tail (0x27 / 0x28 / 0x2e, the shotguns, the sniper rifle): the tail's pair, then the
// shot's pair near or its left sample far.
std::vector<Voice> tailed(int32 tailL, int32 tailR, float32 tailRate, int32 left, int32 right, float32 rate, bool near)
{
    std::vector<Voice> v{{tailL, tailRate * rate, kPanLeft, -1}, {tailR, tailRate * rate, kPanRight, 1}};
    if (near)
    {
        v.push_back({left, rate, kPanLeft, 0});
        v.push_back({right, rate, kPanRight, 0});
    }
    else
        v.push_back({left, rate, -1, 0});
    return v;
}

// A gun without one: the pair near, its left sample far.
std::vector<Voice> pair(int32 left, int32 right, bool near)
{
    if (near)
        return {{left, 1.0f, kPanLeft, 0}, {right, 1.0f, kPanRight, 0}};
    return {{left, 1.0f, -1, 0}};
}

} // namespace

std::vector<Voice> used(int32 ct, int32 value, bool near)
{
    const float32 swingRate = value == 0 ? 0.9f : 1.0f;
    switch (ct)
    {
    // The swings (CT_FISTS, CT_BOTTLE, CT_BAG, CT_KEY, CT_WIRE, the cans and dolls ...).
    case 0x10: case 0x16: case 0x3a: case 0x51: case 0x53: case 0x5a: case 0x5b: case 0x5f: case 0x62: case 0x63: case 0x64:
        return {{kSwingLo, swingRate, -1, 0}};
    // CT_KNIFE, CT_SHARD, CT_CLEAVER, CT_SICKLE, CT_MACHETE, the spikes and Piggsy's shard.
    case 0x12: case 0x13: case 0x18: case 0x1b: case 0x20: case 0x5c: case 0x5e: case 0x60:
        return {{kSwingHi, swingRate, -1, 0}};
    // The bars and bats, CT_AXE, CT_ICEPICK, the brick, CT_HAMMER, the head.
    case 0x1a: case 0x1c: case 0x1e: case 0x1f: case 0x21: case 0x22: case 0x23: case 0x26: case 0x38: case 0x61: case 0x65:
        return {{kSwing, swingRate, -1, 0}};
    case 0x58: // CT_CHAINSAW (a hunter's)
        return {{kSwingLo + value, 1.0f, -1, 0}};
    case 0x27: // CT_6SHOOTER
    case 0x28: // CT_GLOCK
    case 0x2e: // CT_DESERT_EAGLE
        if (value != 0)
            return {};
        return tailed(kGlockTailL, kGlockTailR, 1.0f, kGlockL, kGlockR, ct == 0x27 ? 1.15f : ct == 0x2e ? 0.85f : 1.0f, near);
    case 0x2c: case 0x2d: case 0x32: // CT_SHOTGUN, CT_SHOTGUN_TORCH, CT_SAWNOFF
        if (value != 0)
            return {};
        return tailed(kLongTailL, kLongTailR, 0.8f, kShotgunL, kShotgunR, 1.0f, near);
    case 0x30: // CT_SNIPER_RIFLE
        if (value != 0)
            return {};
        return tailed(kLongTailL, kLongTailR, 0.8f, kSniperL, kSniperR, 1.0f, near);
    case 0x2b: // CT_UZI: each shot, and the burst's end (its tail, then the short tail)
        if (value == 1)
            return {{kLongTailL, 1.1f, kPanLeft, -1}, {kLongTailR, 1.1f, kPanRight, 1}, {kUziShortL, 1.0f, kPanLeft, 0}, {kUziShortR, 1.0f, kPanRight, 0}};
        return pair(kUziL, kUziR, near);
    case 0x2f: // CT_COLT_COMMANDO
        if (value == 1)
            return {{kLongTailL, 1.0f, kPanLeft, -1}, {kLongTailR, 1.0f, kPanRight, 1}, {kColtShortL, 1.0f, kPanLeft, 0}, {kColtShortR, 1.0f, kPanRight, 0}};
        return pair(kColtL, kColtR, near);
    case 0x31: // CT_TRANQ_RIFLE
        if (value != 0)
            return {};
        return pair(kTranqL, kTranqR, near);
    case 0x59: // CT_NAILGUN
        if (value != 0)
            return {};
        return pair(kNailL, kNailR, near);
    default:
        return {}; // SFX_MARKER_END_BANK0 (0x13a): nothing
    }
}

int32 empty(int32 ct)
{
    switch (ct)
    {
    case 0x27: case 0x59: return 0xce; // SFX_WEAPON_FIRE_EMPTY_REVOLVER
    case 0x28: return 0xcf;            // _GLOCK
    case 0x2b: return 0xd4;            // _UZI
    case 0x2c: case 0x2d: case 0x32: return 0xd1; // _SHOTGUN
    case 0x2e: return 0xd0;            // _DESERTEAGLE
    case 0x2f: return 0xd5;            // _COLT
    case 0x30: return 0xd3;            // _SNIPERRIFLE
    case 0x31: return 0xd2;            // _TRANQRIFLE
    default: return -1;
    }
}

int32 shell(int32 ct)
{
    switch (ct)
    {
    case 0x27: case 0x28: case 0x2a: case 0x2b: case 0x2e: case 0x2f: case 0x30:
        return 0x8f; // SFX_WEAPON_SHELL_CASING_METAL
    case 0x2c: case 0x2d: case 0x31: case 0x32:
        return 0x8e; // SFX_WEAPON_SHELL_CASING_PLASTIC
    case 0x59:
        return 0xb6; // SFX_WEAPON_NAIL_TING
    default:
        return -1;
    }
}

} // namespace Game::WeaponAudio
