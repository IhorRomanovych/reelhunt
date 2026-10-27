// Renderer/backend/gpu_types.h - CPU mirrors of the data the shaders read. Must match
// Renderer/backend/frame.glsl exactly (every UBO member is a vec4/mat4, so std140 == C++).
#pragma once
#include "engine_types.h"

#include <glm/glm.hpp>

#include "Renderer/shadow_cascades.h"

namespace Renderer::Backend {

// Lamp shadows: this many spot lights get a pair of shadow maps (static scene, moving casters).
inline constexpr uint32 kMaxShadowLights = 16u;
inline constexpr uint32 kLampShadowSize = 1024u;

// set 0, binding 0: per-frame camera, lighting and shadow data.
struct FrameUniforms {
    glm::mat4 view{1.0f};
    glm::mat4 viewProj{1.0f};
    glm::mat4 invViewProj{1.0f};
    glm::vec4 cameraPos{0.0f};     // xyz
    glm::vec4 viewport{0.0f};      // xy = target size, z = light count, w = LightingMode
    glm::vec4 sunDirection{0.0f};  // xyz = direction the light travels, w = intensity
    glm::vec4 sunColor{0.0f};      // rgb, w = 1 when sun shadows are on
    glm::vec4 ambient{0.0f};       // rgb = default ambient, w = baked-light scale
    glm::mat4 cascadeViewProj[kMaxShadowCascades]{};
    glm::vec4 cascadeSplits{0.0f}; // view distance where each cascade ends
    glm::vec4 cascadeTexel{0.0f};  // world size of a shadow texel per cascade
    glm::vec4 shadowParams{0.0f};  // x = 1 / map size, y = cascade count
    glm::vec4 lampParams{0.0f};    // x = 1 / lamp shadow map size, y = lamp scale, z = baked shadow floor,
                                   // w = debug view (REELHUNT_DEBUG_VIEW: 1 albedo, 2 light, 3 normal, 4 baked flag)
    glm::vec4 sky{0.0f};           // rgb = sky (background) and fog colour
    glm::vec4 fog{0.0f};           // x = fog start, y = fog end (view depth, metres), z = 1 when fog is on
};

// set 0, binding 2: one local light (std430; vertex + fragment stages).
struct GpuLight {
    glm::vec4 positionRadius{0.0f}; // xyz world position, w range (metres)
    glm::vec4 colorIntensity{0.0f}; // rgb colour, a intensity
    glm::vec4 directionCos{0.0f};   // xyz the way a spot shines, w cos(outer half-angle); point: w = -2
    glm::vec4 params{0.0f};         // x cos(inner half-angle), y shadow slot (< 0: none), z RW flags (Light::rwFlags)
    glm::mat4 shadowViewProj{1.0f}; // world -> the light's shadow map (spot lights with a slot)
};

// Push constants for every scene draw (128 bytes, the guaranteed minimum).
struct DrawConstants {
    glm::mat4 model{1.0f};      // rigid model -> world (identity for skinned meshes)
    glm::vec4 material{0.0f};   // x metallic, y roughness, z alpha test (0/1), w baked lighting (0/1)
    glm::vec4 tint{1.0f};       // rgb material colour
    glm::vec4 ambient{0.0f};    // rgb per-object ambient, w > 0 overrides the frame's ambient
    uint32 boneOffset = 0u;     // first matrix of this mesh's pose in the bone buffer
    uint32 cascade = 0u;        // shadow passes: the cascade, or the light whose map is drawn
    uint32 pad[2]{};
};
static_assert(sizeof(DrawConstants) == 128u, "DrawConstants must fill the 128-byte push range");
static_assert(sizeof(GpuLight) == 128u, "GpuLight is 128 bytes in std430");

} // namespace Renderer::Backend
