#pragma once

#include "engine_types.h"

#include <expected>
#include <string>
#include <vector>

#include "Math/math.h"
#include "Math/vector.h"

namespace Assets {

// The game "entity placement" archive (.inst / entinst.bin).
//
// File layout (little-endian on PC; the toolkit switches to big-endian only for
// Wii when the leading count, read as little-endian, is > 100000):
//
//   u32   count
//   u32   sizes[count]            // byte-length of each record block
//   {
//     string  record              // NUL-terminated; block padded to 4-byte boundary
//     string  internalName
//     f32     position[3]
//     f32     rotation[4]         // quaternion (x, y, z, w)
//     string  entityClass         // e.g. "Player_Inst", "Trigger_Inst", "Door_Inst"
//     i32     params[...]         // every remaining 4-byte chunk, in order
//   } x count
//
// String block size = len + (4 - (len % 4)), where len = chars before the first
// NUL. This makes each string occupy a whole number of 32-bit words.
//
// The trailing params are raw values (the game stores no parameter names). Their
// meaning depends on entityClass (see the toolkit's mh1Map, e.g. Player_Inst has a
// single "Hit Points" value; Hunter_Inst has weapon/AI fields, etc.).

struct InstPlacement {
    std::string record;
    std::string internalName;
    Math::Vector3 position = Math::Vector3{0.0f, 0.0f, 0.0f};
    Math::Vector4 rotation = Math::Vector4{0.0f, 0.0f, 0.0f, 1.0f};
    std::string entityClass;
    std::vector<int32> params;
    int32 index = -1; // its place in the level's .inst order (entity.inst, then entity2.inst)
};

class Inst {
public:
    static std::expected<std::vector<InstPlacement>, Engine::EngineError> load(const std::string& path);
};

}  // namespace Assets
