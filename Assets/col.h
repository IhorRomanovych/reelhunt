#pragma once

#include "engine_types.h"

#include <expected>
#include <string>
#include <vector>

#include "Math/math.h"
#include "Math/vector.h"

namespace Assets {

// The game collision archive (.col). Decoded from the Sor3nt's file-format toolkit reference
// (Archive/Col.php + NBinary.php) and validated byte-for-byte against the asylum level.
//
// File layout (little-endian, PC):
//   u32 entryCount
//   per entry:
//     string name            // NUL-terminated; block = len + (4 - (len % 4)) bytes
//     f32 center[3]          // object centre (local frame)
//     f32 radius             // bounding-sphere radius
//     f32 min[3], max[3]     // object AABB (may be a +/-huge "unbounded" sentinel)
//     u32 sphereCount        // each: f32 center[3], f32 radius, i8 surface[4]  = 16 B
//     u32 lineCount          // each: f32 a[3], f32 b[3] (a segment)            = 24 B
//     u32 boxCount           // present in the format; always 0 for the asylum level
//     u32 verticalCount      // each: f32 xyz[3]
//     u32 faceCount          // each: i32 xyz[3]
//
// The primitives are in each object's LOCAL frame. A placed entity/prop uses its own
// entry (matched by name against the .inst internalName) transformed to world space.

struct ColSphere {
    Math::Vector3 center = Math::Vector3{0.0f, 0.0f, 0.0f};
    float32 radius = 0.0f;
    uint8 material = 0;     // surface[0]
    uint8 flag = 0;         // surface[1]
    uint8 brightness = 0;   // surface[2]
    uint8 light = 0;        // surface[3]
};

struct ColLine {
    Math::Vector3 a = Math::Vector3{0.0f, 0.0f, 0.0f};
    Math::Vector3 b = Math::Vector3{0.0f, 0.0f, 0.0f};
};

struct ColFace {
    int32 i0 = 0;
    int32 i1 = 0;
    int32 i2 = 0;
};

struct ColEntry {
    std::string name;
    Math::Vector3 center = Math::Vector3{0.0f, 0.0f, 0.0f};
    float32 radius = 0.0f;
    Math::Vector3 min = Math::Vector3{0.0f, 0.0f, 0.0f};
    Math::Vector3 max = Math::Vector3{0.0f, 0.0f, 0.0f};
    std::vector<ColSphere> spheres;
    std::vector<ColLine> lines;
    uint32 boxCount = 0;                 // always 0 for the asylum level (no decoder)
    std::vector<Math::Vector3> verticals;
    std::vector<ColFace> faces;
};

struct ColData {
    std::vector<ColEntry> entries;
    uint32 totalSpheres = 0;
    uint32 totalLines = 0;
    uint32 totalBoxes = 0;
    uint32 totalVerticals = 0;
    uint32 totalFaces = 0;

    // First entry whose name matches (case-sensitive), or nullptr.
    const ColEntry* find(const std::string& name) const noexcept {
        for (const auto& e : entries)
            if (e.name == name) return &e;
        return nullptr;
    }
};

class Col {
public:
    // Parse the .col archive. Fails with Invalid if any entry is malformed or a box
    // block is present (the asylum level has none), or if trailing bytes remain.
    static std::expected<ColData, Engine::EngineError> load(const std::string& path);
};

}  // namespace Assets
