// Game/execution.cpp - executions. See execution.h.
#include "Game/execution.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace Game {

std::vector<std::vector<CameraShot>> parseCameraShots(const Assets::IniRecord& record)
{
    std::vector<std::vector<CameraShot>> sets(1);
    for (const auto& line : record.lines)
    {
        if (line.size() < 8 || !(Assets::iequals(line[0], "VECPAIR") || Assets::iequals(line[0], "VECPAIR@")))
            continue;
        auto n = [&](std::size_t i) { return i < line.size() ? Assets::parseIniNumber(line[i]).value_or(0.0f) : 0.0f; };
        CameraShot s;
        s.duration = std::max(0.05f, n(1));
        s.eye = {n(2), n(3), n(4)};
        s.target = {n(5), n(6), n(7)};
        s.roll = n(9);
        sets.back().push_back(s);
        if (Assets::iequals(line[0], "VECPAIR@"))
            sets.emplace_back();
    }
    if (sets.back().empty())
        sets.pop_back();
    return sets;
}

CameraPose cameraPoseAt(const std::vector<CameraShot>& set, float32 t, const Math::Vector3& feet, float32 yaw)
{
    CameraPose pose;
    if (set.empty())
        return pose;
    // The shot playing at t (the last one holds once the set has run out).
    float32 start = 0.0f;
    std::size_t i = 0;
    while (i + 1 < set.size() && t >= start + set[i].duration)
        start += set[i++].duration;
    const CameraShot& s = set[i];
    pose.shot = static_cast<int32>(i);
    // Player frame -> scene: y ahead, x to his right, z up.
    const Math::Vector3 ahead{-std::sin(yaw), 0.0f, -std::cos(yaw)};
    const Math::Vector3 right{std::cos(yaw), 0.0f, -std::sin(yaw)};
    auto place = [&](const Math::Vector3& p) { return feet + right * p.x + ahead * p.y + Math::Vector3{0.0f, p.z, 0.0f}; };
    pose.eye = place(s.eye);
    pose.target = place(s.target);
    return pose;
}

int32 executionClass(int32 ct)
{
    switch (ct) // COLLECTABLE_TYPE values (config/script_constants.txt)
    {
    case 58: return 0;                   // CT_BAG
    case 18: case 19: return 1;          // CT_KNIFE, CT_SHARD
    case 26: return 2;                   // CT_CROWBAR
    case 34: case 35: case 38: return 3; // CT_BASEBALL_BAT, CT_W_BASEBALL_BAT, CT_BASEBALL_BAT_BLADES
    case 27: return 4;                   // CT_SICKLE
    case 90: return 5;                   // CT_WIRE
    case 24: case 32: return 6;          // CT_CLEAVER, CT_MACHETE
    case 30: case 31: return 7;          // CT_AXE, CT_ICEPICK
    case 28: case 33: return 8;          // CT_NIGHTSTICK, CT_SMALL_BAT
    case 97: return 9;                   // CT_HAMMER
    case 108: return 10;                 // CT_CHAINSAW_PLAYER
    case 94: return 11;                  // CT_PIGSY_SHARD
    case 95: return 12;                  // CT_PIGSY_WIRE
    case 92: case 96: return 13;         // CT_WOODEN_SPIKE, CT_PIGSY_SPIKE
    default: return -1;
    }
}

const char* executionClassName(int32 cls)
{
    static constexpr const char* kNames[] = {"Bag",    "Knife",     "Crowbar", "Bat",      "Sickle",       "Wire",        "Cleaver",
                                             "IceAxe", "BlackJack", "Hammer",  "Chainsaw", "Shard_Piggsy", "Wire_Piggsy", "Spike_Piggsy"};
    return cls >= 0 && cls < static_cast<int32>(std::size(kNames)) ? kNames[cls] : "";
}

std::string executionSoundFile(int32 ct, int32 level, const std::string& levelName)
{
    struct Sound {
        const char* folder;
        const char* stem;
        bool numbered;
    };
    Sound s{};
    switch (ct)
    {
    case 30: case 31: s = {"AXE", "AXE", true}; break;         // axe, ice pick
    case 58: s = {"BAG", "BAG", true}; break;
    case 34: case 35: case 38: s = {"BASEBALL", "BBAT", true}; break;
    case 24: case 32: s = {"CLEAVER", "CLEAVE", true}; break; // cleaver, machete
    case 26: s = {"CROWBAR", "CROW", true}; break;
    case 97: s = {"HAMMER", "HAMMER", true}; break;
    case 28: case 33: s = {"JACK", "JACK", true}; break;      // nightstick, small bat
    case 94: s = {"PSHARD", "PSHARD", false}; break;
    case 92: case 96: s = {"PSPIKE", "PSPIKE", false}; break;
    case 18: case 19: s = {"SHARD", "SHARD", true}; break;    // knife, shard
    case 27: s = {"SICKLE", "SICKLE", true}; break;
    case 90: s = {"WIRE", "WIRE", true}; break;
    case 108: s = {"CHAINSAW", "CHAIN", true}; break;
    default: return {};                                        // (CT_PIGSY_WIRE has none)
    }
    if (s.numbered && (level < 2 || level > 4))
        return {};
    const bool cerberus = Assets::iequals(levelName, "Estate_Ext") || Assets::iequals(levelName, "Mansion_Int") ||
                          Assets::iequals(levelName, "Attic");
    std::string file = std::string(s.folder) + "/" + s.stem;
    if (s.numbered)
        file += std::to_string(level - 1);
    return file + (cerberus ? "B" : "A") + ".RIB";
}

ExecutionPlan planExecution(const ItemType& item, int32 level, const std::string& levelName, const std::string& victim)
{
    ExecutionPlan plan;
    const bool ramirez = Assets::iequals(victim, "Ram_Bod1");
    if (ramirez)
        level = 2;
    else if (Assets::iequals(victim, "Pig_BodF1") || Assets::iequals(victim, "Sw_Bod") || Assets::iequals(victim, "Sw_Bod_Gore"))
        level = 1;
    const ExecutionLevel* e = item.execution(level);
    const int32 cls = executionClass(item.ct);
    if (!e || cls < 0)
        return plan;
    plan.level = level;
    plan.data = e;
    // Execution n of a class: <Class>_Sneak_Attack<n> (the Piggsy kills and Ramirez's have one
    // clip, no number, and no Die_ clip).
    std::string clip = std::string(ramirez ? "Ramirez" : executionClassName(cls)) + "_Sneak_Attack";
    const bool numbered = cls <= 10 && !ramirez;
    if (numbered)
        clip += std::to_string(level);
    plan.playerClip = clip;
    plan.hunterClip = "Damage_" + clip;
    if (numbered)
        plan.dieClip = "Die_" + clip;
    plan.soundFile = executionSoundFile(item.ct, level, levelName);
    return plan;
}

const char* executionNoise(const ItemType& item, int32 level)
{
    const bool green = item.halo.shown && item.halo.r == 0 && item.halo.g == 80 && item.halo.b == 0; // BELT_LEFT
    if (green || level <= 2)
        return "EXECUTION_VERY_LOW";
    return level == 3 ? "EXECUTION_LOW" : "EXECUTION_MEDIUM";
}

int32 ExecutionCharge::level(const ItemType& item) const
{
    const int32 reached0 = held_ >= stages_[1] ? 4 : (held_ >= stages_[0] ? 3 : 2);
    int32 reached = reached0;
    while (reached > 2 && !item.execution(reached))
        --reached;
    return item.execution(reached) ? reached : 0;
}

} // namespace Game
