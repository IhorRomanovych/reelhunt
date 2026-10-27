// Renderer/skin_pose.h - one skinned model's current bone matrices.
#pragma once
#include "engine_types.h"

#include <vector>

namespace Renderer {

// Bone indices in a skinned vertex stream are bytes, so a skin has at most 256 bones.
inline constexpr uint32 kMaxSkinBones = 256u;

// The pose a set of skinned meshes is drawn with: one column-major model->world matrix per
// bone (16 floats each). The owner (a Character) rewrites it every frame; every skinned mesh
// that shares the pose (body parts of one model) points at it. The renderer gathers all poses
// in use into one bone buffer per frame, so any number of characters can be on screen.
struct SkinPose {
    std::vector<float> matrices;

    [[nodiscard]] uint32 boneCount() const noexcept { return static_cast<uint32>(matrices.size() / 16u); }
};

} // namespace Renderer
