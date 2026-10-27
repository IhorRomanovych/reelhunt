#pragma once

#include "engine_types.h"

#include <array>
#include <expected>
#include <unordered_map>
#include <string>
#include <vector>

#include "Math/math.h"
#include "Math/vector.h"

namespace Assets {

struct DffVertex {
    Math::Vector3 position;
    Math::Vector3 normal;
    Math::Vector2 uv;
    Math::Vector4 color = Math::Vector4{1.0f, 1.0f, 1.0f, 1.0f}; // prelight (baked light), RGBA 0..1
};

struct DffTriangle {
    uint32 a = 0;
    uint32 b = 0;
    uint32 c = 0;
    uint32 materialIndex = 0;
};

struct DffMaterial {
    Math::Vector4 color = Math::Vector4{1.0f, 1.0f, 1.0f, 1.0f};
    std::string textureName;
};

// One skeleton bone: the HAnim id (for IFP boneId -> index mapping) plus the matrix
// ingredients needed to build a per-frame skin matrix. All matrices are stored
// column-major so they can be uploaded straight into a Vulkan UBO.
//   localRest    : bind-pose LOCAL transform = Frame::matrix (rotation + joint offset).
//                  This is what IFP keyframes replace/interpolate at runtime.
//   restLocal    : bind-pose WORLD transform (localRest composed through the hierarchy);
//                  kept as a reference / identity-skinning fallback.
//   inverseBind  : bind correction = Skin::inverseMatrices[i] (= inv(worldRest)).
//   nodeFlags    : HAnim PUSH/POP flags, used to replicate updateMatrices() composition.
struct DffBone {
    int32 id = 0;
    uint32 nodeFlags = 0u;
    float localRest[16]{};
    float restLocal[16]{};
    float inverseBind[16]{};
};

struct DffAtomic {
    std::vector<DffVertex> vertices;
    std::vector<DffTriangle> triangles;
    std::vector<DffMaterial> materials;
    bool prelit = false; // the geometry carries per-vertex prelight colours (rpGEOMETRYPRELIT)
    // Its frame's name (the node-name chunk 0x0253F2FE), e.g. "Gen_Bin_Liner_L1": a prop's
    // damage LODs are its atomics "<model>_L<n>" (LOD_DATA). Empty when the frame has none.
    std::string frameName;

    // Skinning (empty when the atomic is not skinned). boneIndices/weights are
    // per-vertex, parallel to `vertices`.
    bool skinned = false;
    int32 numBones = 0;
    std::vector<std::array<uint8_t, 4>> boneIndices;
    std::vector<std::array<float, 4>> weights;
};

struct DffClump {
    std::vector<DffAtomic> atomics;

    // Skeleton (empty when the clump is not skinned). localSpaceMatrices mirrors
    // HAnimHierarchy::LOCALSPACEMATRICES and selects the skin-matrix formula.
    std::vector<DffBone> bones;
    bool localSpaceMatrices = false;

    // Each named frame's origin in the clump's space (the frame list composed through its
    // parents), e.g. a lamp's "HOTSPOT" where its light sits. Names as stored.
    std::vector<std::pair<std::string, Math::Vector3>> framePoints;
};

class Dff {
public:
    static std::expected<std::vector<DffClump>, Engine::EngineError>
    load(const std::string& path);
};

// Model (record) name -> index of the top-level clump that defines it, from the raw .dff bytes
// (Dff::load keeps no names). Names with no clump are absent from the map.
std::unordered_map<std::string, int> clumpIndexByName(const std::vector<uint8_t>& bytes,
                                                      const std::vector<std::string>& records);

}  // namespace Assets
