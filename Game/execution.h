// Game/execution.h - executions (G5): sneaking up behind a hunter with an execution weapon and
// killing it in one of three levels of violence.
//
// The player locks on to an unaware hunter from behind (LevelRuntime::executionTarget) and holds
// the attack button: the longer he holds, the more violent the kill - execution 2 (hasty, white),
// 3 (violent, yellow), 4 (gruesome, red); execution 1 is unused ("execution 1 not used now" in
// entityTypeData.ini). Releasing starts the kill of the reached level. The charge time per level
// is ours.
//
// The clips come from the weapon's kind of kill, by its COLLECTABLE_TYPE (exe 0x4c93b0;
// see executionClass): execution n of class Bag plays the player's Bag_Sneak_Attack<n>, the
// hunter's Damage_Bag_Sneak_Attack<n> and then Die_Bag_Sneak_Attack<n> (clip id 0x2e0 + class * 4
// + n - 1, 0x41e480). Both clips share one origin. The record gives the rest for a level
// (Game/items.h): EXECUTE_TVP EXECUTE_BAG2 -> levels/global/tvp/TimedVectorPairs.ini, the camera
// shots; EXECUTE_MODEL / EXECUTE_MODEL_ANIM, the weapon's own model and clip (Bag_Sneak_Attack2_W),
// which only some weapons have.
// The sound is one mono stream, audio/PC/EXECUTE/<folder>/<stem><n - 1><A|B>.RIB, by the weapon's
// kind (0x5a5370): A, or B in the Cerberus levels (see executionSoundFile).
//
// TimedVectorPairs records hold sets of shots; "VECPAIR@" closes a set (EXECUTE_BAG2: three sets
// of three, each set ~7.8 s against the 8.2 s clip) and one set is picked per kill. A shot is
//   VECPAIR <seconds> <eye x y z> <target x y z> 0.5 <roll degrees>
// in the player's frame, Z up: y ahead (towards the hunter), x to the side, z height.
#pragma once
#include "engine_types.h"

#include <array>
#include <random>
#include <string>
#include <vector>

#include "Assets/ini_records.h"
#include "Game/items.h"
#include "Math/vector.h"

namespace Game {

struct CameraShot {
    float32 duration = 1.0f;
    Math::Vector3 eye{0.0f, 0.0f, 0.0f};    // player frame: x side, y ahead, z up
    Math::Vector3 target{0.0f, 0.0f, 0.0f};
    float32 roll = 0.0f;                    // degrees
};

// The sets of shots of one TimedVectorPairs record (empty when it has none).
std::vector<std::vector<CameraShot>> parseCameraShots(const Assets::IniRecord& record);

// Where a set of shots puts the camera `t` seconds in, for a player at `feet` facing `yaw`
// (0 = scene -Z): eye and target in the scene, and which shot it is (cuts change it).
struct CameraPose {
    Math::Vector3 eye{0.0f, 0.0f, 0.0f};
    Math::Vector3 target{0.0f, 0.0f, 0.0f};
    int32 shot = 0;
};
CameraPose cameraPoseAt(const std::vector<CameraShot>& set, float32 t, const Math::Vector3& feet, float32 yaw);

// The clips, sound and timing of one execution level of an item.
struct ExecutionPlan {
    int32 level = 0;
    std::string playerClip, hunterClip, dieClip; // dieClip: none for the Piggsy kills
    std::string soundFile; // under audio/PC/EXECUTE/: BAG/BAG2A.RIB ("" = none)
    const ExecutionLevel* data = nullptr;
};

// The kind of kill a weapon makes, by its COLLECTABLE_TYPE value (exe 0x4c93b0): 0 Bag,
// 1 Knife (knife, shard), 2 Crowbar, 3 Bat (the three baseball bats), 4 Sickle, 5 Wire, 6 Cleaver
// (cleaver, machete), 7 IceAxe (axe, ice pick), 8 BlackJack (nightstick, small bat), 9 Hammer,
// 10 Chainsaw (the player's), 11 Shard_Piggsy, 12 Wire_Piggsy, 13 Spike_Piggsy (the wooden spike
// too); -1 none (the hunters' chainsaw, guns, ...). The exe's 14, Ramirez_Sneak_Attack, is
// chosen by the victim (Ram_Bod1), not the weapon.
int32 executionClass(int32 ct);
// Its clips' stem: "Bag" for class 0 ... "Spike_Piggsy" for 13 ("" for none).
const char* executionClassName(int32 cls);

// The sound of execution `level` (2..4) with a weapon of COLLECTABLE_TYPE `ct` in level folder
// `levelName` (exe 0x5a5370 / 0x5a5690, stream table 0x6abae0): <folder>/<stem><level -
// 1><A|B>.RIB, the Piggsy kills <folder>/<stem><A|B>.RIB. A and B are two takes, not stereo
// sides: the exe streams one of them, B when the level index + 1 is 18..20 (Estate_Ext,
// Mansion_Int, Attic: the Cerberus mercenaries), A elsewhere. "" when the weapon has none.
std::string executionSoundFile(int32 ct, int32 level, const std::string& levelName);

// The plan for `level` of `item` in level folder `levelName` on a victim of model record `victim`;
// level 0 when the item has no such execution. The bosses fix the kill whatever the charge
// (0x46ce0a..0x46cebb): Ramirez (Ram_Bod1) dies by Ramirez_Sneak_Attack as execution 2, Piggsy
// (Pig_BodF1) and Starkweather (Sw_Bod, Sw_Bod_Gore) by the weapon's execution 1 (the player's
// chainsaw: STARKWEATHER_DIE2).
ExecutionPlan planExecution(const ItemType& item, int32 level, const std::string& levelName = {},
                            const std::string& victim = {});

// The AISounds.ini sound other hunters hear while a kill of `level` with `item` plays. The level
// text says green weapons "allow you to execute in complete silence" and that hunters close by
// can hear an execution: green items give EXECUTION_VERY_LOW (heard within 5 m), the rest by
// level - 2 EXECUTION_VERY_LOW, 3 EXECUTION_LOW, 4 EXECUTION_MEDIUM (ours).
const char* executionNoise(const ItemType& item, int32 level);

// The charge: how long the button has been held -> the level reached (2, 3 or 4, never past the
// item's last level). The player record's EXECUTE_STAGE_TIMES (2.0, 5.0 s) are when 3 and 4 are
// reached.
class ExecutionCharge {
public:
    void setStageTimes(float32 violent, float32 gruesome) noexcept { stages_ = {violent, gruesome}; }
    void start() noexcept { held_ = 0.0f; charging_ = true; }
    void cancel() noexcept { charging_ = false; }
    void update(float32 dt) noexcept { if (charging_) held_ += dt; }
    [[nodiscard]] bool charging() const noexcept { return charging_; }
    [[nodiscard]] int32 level(const ItemType& item) const;

private:
    float32 held_ = 0.0f;
    bool charging_ = false;
    std::array<float32, 2> stages_{2.0f, 5.0f};
};

} // namespace Game
