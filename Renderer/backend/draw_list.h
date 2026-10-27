// Renderer/backend/draw_list.h - one frame's draws gathered from the scene: GPU meshes with their
// push constants, every skin pose packed into one bone array, and the point lights.
#pragma once
#include "engine_types.h"

#include <array>
#include <glm/glm.hpp>
#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/backend/gpu_types.h"
#include "Renderer/scene.h"

namespace Renderer::Backend {

class ResourceCache;
struct GpuMesh;

struct DrawItem {
    const GpuMesh* mesh = nullptr;
    VkDescriptorSet material = VK_NULL_HANDLE;
    DrawConstants constants;
    glm::vec3 center{0.0f}; // world bounding sphere (radius < 0 = never culled)
    float32 radius = -1.0f;
    bool skinned = false;
    bool castsShadow = true;
    bool moving = false;      // characters, movable props: redrawn into the lamp shadows every frame
    bool translucent = false; // alpha-blended: forward pass, not the G-buffer
    bool additive = false;    // translucent, added to what is behind (source alpha, one)
};

struct DrawList {
    std::vector<DrawItem> items;
    std::vector<float> bones;     // column-major mat4s, every pose in use
    std::vector<GpuLight> lights;
    uint32 shadowLights = 0u;     // lights with a shadow slot (slots 0 .. shadowLights-1)
    uint64 shadowSignature = 0u;  // changes when a shadowed light or the static casters change

    void build(const Scene& scene, ResourceCache& cache);
};

// The six planes of a view-projection's frustum (Vulkan depth 0..1), for sphere culling.
class Frustum {
public:
    explicit Frustum(const glm::mat4& viewProj);
    [[nodiscard]] bool visible(const DrawItem& item) const noexcept;

private:
    std::array<glm::vec4, 6> planes_{};
};

} // namespace Renderer::Backend
