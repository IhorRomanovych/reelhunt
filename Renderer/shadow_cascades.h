// Renderer/shadow_cascades.h - cascaded shadow map fitting for a directional light (pure math).
//
// The view frustum is cut into slices at increasing distances; each slice gets its own
// orthographic light camera, fitted to the slice's bounding sphere (so its size does not change
// as the camera turns) and snapped to whole shadow-map texels (so edges do not shimmer as the
// camera moves). Near slices cover a few metres at high resolution, far ones whole streets.
#pragma once
#include "engine_types.h"

#include <array>
#include <glm/glm.hpp>

namespace Renderer {

inline constexpr uint32 kMaxShadowCascades = 4u;

struct CascadeCamera {
    glm::vec3 position{0.0f};
    glm::mat4 view{1.0f};      // world -> camera view space (looking down -Z)
    float32 fovYDegrees = 50.0f;
    float32 aspect = 1.0f;
};

struct ShadowCascade {
    glm::mat4 viewProj{1.0f};   // world -> light clip space (Vulkan depth 0..1)
    float32 splitFar = 0.0f;    // view-space distance where this cascade ends
    float32 texelWorld = 0.0f;  // world size of one shadow-map texel
};

struct CascadeFit {
    std::array<ShadowCascade, kMaxShadowCascades> cascades{};
    uint32 count = 0u;
};

// Fits `splits.size()` cascades: cascade i covers view distances [splits[i-1] (or nearPlane),
// splits[i]]. `lightDir` points the way the light travels. `casterMargin` extends each light
// camera backwards so occluders outside the view (a wall behind the camera) still cast.
CascadeFit fitShadowCascades(const CascadeCamera& camera, float32 nearPlane, const float32* splits, uint32 splitCount,
                             const glm::vec3& lightDir, uint32 mapSize, float32 casterMargin = 120.0f);

} // namespace Renderer
