// Game/checkpoint.h - a level's live state as a save's entity block, and back (backlog D).
//
// The exe writes the entity block (0x43a2c0) by walking its entity registry, which is the
// level's .inst placements in file order (entity.inst, then entity2.inst; the scripts' spawns
// after them): a placement whose entity is gone (a pickup taken, a hunter dead, a save point
// used) is "NUL"; any other is its record and instance name, its position in the .inst frame,
// its rotation as RenderWare's Y-up quaternion ((x, -z, y, w) of the .inst one), "wibble", and
// its class data - the .inst parameters with the live values in them (vtable +0xc). A save
// point's checkpoint (0x4d2400) keeps the block; loading one builds the level's entities from it
// instead of the .inst files (0x474330 -> 0x437650(0)).
//   Class data seen against the .inst parameters (Jury_Turf, a real checkpoint save):
//   Base_Inst / Player_Inst: [health]; Hunter_Inst: 9 words, health first;
//   Door_Inst: [health, 0, state]; Trigger_Inst (save points too): [state, float];
//   CameraData: word 1, Hunter_Inst word 8, Door_Inst word 2 and Light_Inst words 6..12 keep their low
//   bits (& 0x3ff); Light_Inst word 1 one less.
#pragma once
#include "engine_types.h"

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/inst.h"
#include "Game/save_system.h"
#include "Math/vector.h"

namespace Game {

struct LevelFrame;

namespace Checkpoint {

// The .inst quaternion of a save record's rotation, and back.
[[nodiscard]] Math::Vector4 instRotation(const std::vector<float>& plane);
[[nodiscard]] std::vector<float> saveRotation(const Math::Vector4& inst);
// The .inst position of a scene-frame point, and the .inst quaternion of a scene yaw (as
// LevelFrame::placementYaw / characterYaw read them back).
[[nodiscard]] Math::Vector3 instPosition(const LevelFrame& frame, const Math::Vector3& scene);
[[nodiscard]] Math::Vector4 instYawRotation(float32 placementYaw);

// The global game script's data (levels/GLOBAL/game.scc's DATA chunk, its unset bytes 0xda as
// zero): a save's buffer 4 (0x4812d0 copies the script's memory, 0x71c87c).
[[nodiscard]] std::vector<std::uint8_t> globalScriptData(const std::string& sccPath);

// The "NUL" record of a gone entity.
[[nodiscard]] SaveEntityRecord nulRecord();
// A placement's record as the exe writes it, from its .inst parameters (class data per class as
// above); the caller puts the live values in.
[[nodiscard]] SaveEntityRecord record(const Assets::InstPlacement& p);
// A class data word, read and written.
[[nodiscard]] std::uint32_t word(const SaveEntityRecord& r, std::size_t i);
void setWord(SaveEntityRecord& r, std::size_t i, std::uint32_t v);

// The level's placements as a checkpoint has them (0x437650(0)): each one overwritten by its
// record (position, rotation, parameters = the class data) and the "NUL"s dropped. Each
// placement's `index` (its place in the .inst order) is what lines them up.
void apply(std::vector<Assets::InstPlacement>& placements, const std::vector<SaveEntityRecord>& records);

} // namespace Checkpoint
} // namespace Game
