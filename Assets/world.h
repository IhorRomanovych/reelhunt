#pragma once

#include "engine_types.h"

#include <expected>
#include <string>
#include <vector>

#include "Math/vector.h"

namespace Assets {

// The game RWWorld level geometry (.bsp).
//
// File layout (little-endian, PC): a single top-level ID_WORLD chunk (type 0x0B) whose content
// is a sequence of RenderWare chunks: STRUCT (level header) + MATLIST (type 8) + a recursive
// octree + EXTENSION. Octree nodes are type-10 chunks holding [STRUCT(bounds/flags)] plus child
// nodes or type-9 leaves; a leaf holds [STRUCT(mesh data)] + EXTENSION.
//
// Mesh STRUCT layout (little-endian words), validated by msize/4 == 11 + 7*NV + 2*NT:
//   [0] flags  [1] NT (triangles)  [2] NV (vertices)  [3..8] bbox min/max  [9,10] reserved
//   [11 .. 11+3NV)        positions (NV x f32 xyz)
//   [11+3NV .. 11+7NV)    per-vertex block: NV words of i8 normal (xyz + pad), NV words of
//                         prelight colour (u8 R, G, B, A - the level's baked lighting), then
//                         NV x f32 (u,v) pairs
//   [11+7NV .. )          triangles: NT x 4 u16. RW 3.6 files store (a, b, c, materialIndex);
//                         RW 3.4 files (most the game levels) store (materialIndex, a, b, c).
//                         Verified across all 48 shipped scene*.bsp files.
//
// The level is authored Y-up (y = height); positions are returned as stored.

struct WorldTriangle {
    uint32 a = 0;
    uint32 b = 0;
    uint32 c = 0;
    uint32 materialIndex = 0;  // index into WorldData::materialNames
};

struct WorldMesh {
    std::vector<Math::Vector3> positions;
    std::vector<Math::Vector3> normals;    // per-vertex, unit length (int8-packed in the file)
    std::vector<Math::Vector2> uvs;        // per-vertex texture coordinates
    std::vector<Math::Vector4> prelight;   // per-vertex baked light colour, RGBA in 0..1
    std::vector<WorldTriangle> triangles;  // only triangles whose indices are all < positions.size()
    Math::Vector3 bboxMin = Math::Vector3{0.0f, 0.0f, 0.0f};
    Math::Vector3 bboxMax = Math::Vector3{0.0f, 0.0f, 0.0f};
};

struct WorldData {
    std::vector<WorldMesh> meshes;
    // One entry per MATLIST material, in material-index order. Empty for an untextured material.
    std::vector<std::string> materialNames;
    uint32 leafCount = 0;
    uint32 totalVerts = 0;
    uint32 totalTris = 0;
    uint32 layoutInvariantOk = 0;  // leaves whose size matches 11 + 7*NV + 2*NT words
    uint32 droppedTriangles = 0;   // triangles discarded for an out-of-range vertex index
    uint32 rwLibraryId = 0;        // RenderWare library id of the WORLD chunk, e.g. 0x36003
};

class World {
public:
    // Parse the .bsp octree and extract every leaf mesh plus the material list.
    static std::expected<WorldData, Engine::EngineError> load(const std::string& path);

    // Diagnostic: print the parse summary and the first leaves to stderr.
    static void dump(const std::string& path);
};

}  // namespace Assets
