// Game/items.h - the level's weapons and collectables (entityTypeData.ini, CLASS EC_WEAPON /
// EC_COLLECTABLE).
//
// A pickup is an ordinary Base_Inst placement whose record has one of those classes (Jury_Turf:
// Bag_(CT) x6, Shard_(CT) x5, G_First_Aid_(CT) x7). The record gives:
//   COLLECTABLE_TYPE  CT_BAG ...        the eCollectableType the scripts see (config/script_constants.txt,
//                                       from the exe's COLLECTABLE_TYPE parser: CT_BAG 58, CT_SHARD 19)
//   WEAPON            fists / shard     its WeaponTypeData record (the bag and wire fight as fists)
//   HOTSPOT_RADIUS    0.75              how close the player must come to pick it up
//   DISPLAY_SCALE, MODEL_DISPLAY_ANIM   how it shows while lying there (bigger, spinning: Bag_Spin_W)
//   BND_SPH_BOOST     1.3               its bounding sphere times this (type +0xcc; not the halo's size)
//   HOLSTER_SLOT / _TRANSLATION / _ROTATION   where it hangs while carried
//   per execution (execution 1 is unused; NEXT_EXECUTION starts the next): EXECUTE_TVP (the camera,
//   TimedVectorPairs.ini), EXECUTE_MODEL(_ANIM), EXECUTE_HUNTER_WEAPON_DROP_TIME,
//   EXECUTE_PLAYER_WEAPON_DESTROY_TIME, EXECUTE_HEAD_SEVER_TIME
// Items are colour-coded by type ("Small improvised weapons, like the Glass Shard are Green",
// pc_jury_turf.gxt H_SHARD): the halo around one lying in the level (Game/pickup_halo.h) takes
// its colour from its HOLSTER_SLOT (BELT_LEFT green, BELT_RIGHT blue, BACK red, ...), first aid
// and ammo boxes from their COLLECTABLE_TYPE.
#pragma once
#include "engine_types.h"

#include <string>
#include <vector>

#include "Assets/ini_records.h"
#include "Game/pickup_halo.h"
#include "Game/script/script_compiler.h"
#include "Math/vector.h"

namespace Game {

struct ExecutionLevel {
    std::string tvp;                  // camera shots (TimedVectorPairs.ini record)
    std::string model, modelAnim;     // the weapon's own model / clip during the kill
    // EXECUTE_BONE: the holder's bone that carries the weapon (and its EXECUTE_MODEL) through the
    // kill; 0 = none given, the hand weapons are held in (1075); 1000 = the holder's own matrix
    // (exe 0x4c8170: the held weapon copies that bone's matrix every frame). Only the
    // sickle sets one (1094).
    int32 bone = 0;
    float32 hunterWeaponDrop = 0.0f;  // seconds into the kill
    // EXECUTE_PLAYER_WEAPON_DESTROY_TIME: when a single-use weapon (bag, wire, shard) is gone from
    // his hand; < 0 = never (bats, crowbars and blades are kept after the kill).
    float32 playerWeaponDestroy = -1.0f;
    float32 headSever = 0.0f;         // 0 = never
    // EXECUTE_HEAD_EXPLODE_TIME: when the victim's head bursts (the bats' and the chainsaw's
    // gruesome kills); 0 = never.
    float32 headExplode = 0.0f;
};

// A gun's looks when it fires (its entityTypeData record): FLASH_MODEL at FLASH_POS (the gun
// model's space: +X runs down the barrel), and the shells: SHELL_MODEL, SHELL_NUMBER of them from
// the SHELL_EJECT_POS points, SHELL_EJECT_DELAY_FIRE seconds after a shot or
// SHELL_EJECT_DELAY_RELOAD into a reload (the revolver and sawn-off drop theirs then), up to
// SHELL_RAND_DELAY later each, flying at SHELL_VELOCITY (gun space, give or take SHELL_VEL_RAND of
// it) and turning SHELL_SPIN; SHELL_RADIUS on the floor. SHELLS_FIRED: the "shell" is what the gun
// fires (the nail gun's nails).
struct GunLook {
    std::string flashModel;
    Math::Vector3 flashPos{0.0f, 0.0f, 0.0f};
    std::string shellModel;
    int32 shellNumber = 1;
    std::vector<Math::Vector3> ejectPos;
    float32 ejectDelayFire = -1.0f;   // < 0: none on firing
    float32 ejectDelayReload = -1.0f; // < 0: none on reloading
    float32 randDelay = 0.0f;
    Math::Vector3 velocity{0.0f, 0.0f, 0.0f};
    float32 velRand = 0.0f;
    Math::Vector3 spin{0.0f, 0.0f, 0.0f};
    float32 radius = 0.01f;
    bool shellsFired = false;
    int32 ct = 0; // its item's collectable type (a landing shell's sound, Game/weapon_audio.h)
};

struct ItemType {
    std::string record;          // entityTypeData record, e.g. "Bag_(CT)"
    std::string model;           // MODEL
    std::string collectable;     // "CT_BAG"
    int32 ct = 0;                // its eCollectableType value
    bool weapon = false;         // EC_WEAPON, or an EC_RESPONDER with a WEAPON (the bottle, brick, can and
                                 // severed head: thrown, carried in BELT_REAR); else EC_COLLECTABLE
    bool smashable = false;      // SMASHABLE / SMASHABLE2: thrown, it breaks where it lands (the bottle)
    std::string material;        // MATERIAL (MATERIAL_BOTTLE ...): the sound it makes landing
    std::string weaponType;      // WEAPON: WeaponTypeData record
    std::string kickWeapon;      // KICK_WEAPON: its kick record (kick_fists, kick_beater ...)
    std::string obstructWeapon;  // OBSTRUCT_WEAPON: the blow with it when someone is in its way
                                 // (obstruct_1handedGun / _2handedGun / obstruct_melee)
    float32 hotspot = 0.75f;
    float32 displayScale = 1.0f;
    float32 boundsBoost = 1.0f;  // BND_SPH_BOOST
    // TORCH_LIGHT: a torch on it (Shotgun_Torch_(CT)) at TORCH_LIGHT_POS (its model's space, +X
    // down the barrel), shining down the barrel turned by TORCH_LIGHT_ORIEN (degrees about x, y, z).
    bool torch = false;
    Math::Vector3 torchPos{0.0f, 0.0f, 0.0f};
    Math::Vector3 torchOrien{0.0f, 0.0f, 0.0f};
    PickupHalo halo;             // how it shows lying in the level (pickupHalo)
    std::string displayAnim;     // MODEL_DISPLAY_ANIM
    std::string holsterSlot;     // HOLSTER_SLOT (BELT_LEFT, ...)
    Math::Vector3 holsterTranslation{0.0f, 0.0f, 0.0f};
    Math::Vector4 holsterRotation{0.0f, 0.0f, 0.0f, 1.0f}; // quaternion x, y, z, w
    std::vector<ExecutionLevel> executions; // [0] = execution 1
    GunLook gun;                            // (guns only)

    // Execution `level` (2..4: hasty, violent, gruesome); nullptr when the item has none.
    [[nodiscard]] const ExecutionLevel* execution(int32 level) const;
    [[nodiscard]] bool canExecute() const;
};

// The player's inventory slot of an item: the exe's HOLSTER_SLOT parser (0x440158) stores
// BACK 1, BELT_RIGHT 2, BELT_LEFT 3, BELT_REAR 4, SPECIAL 5, none 0 (the type data's +0x168);
// the inventory keeps one item in each, and a save keeps slots 1..4 (Game/save_system.h).
int32 inventorySlot(const std::string& holsterSlot);

class ItemCatalog {
public:
    // The EC_WEAPON / EC_COLLECTABLE records of entityTypeData.ini; `constants` gives CT values.
    void read(const std::vector<Assets::IniRecord>& records, const Script::ConstantTable& constants);
    [[nodiscard]] const ItemType* find(const std::string& record) const;
    [[nodiscard]] const ItemType* byCollectable(int32 ct) const;
    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }

private:
    std::vector<ItemType> items_;
};

} // namespace Game
