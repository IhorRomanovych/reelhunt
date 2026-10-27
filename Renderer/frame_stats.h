// Renderer/frame_stats.h - what a rendered frame cost: GPU time per pass and what was drawn.
#pragma once
#include "engine_types.h"

#include <array>
#include <cstddef>

namespace Renderer {

// The GPU passes in recording order (see Renderer/backend/vulkan.h).
enum class GpuPass : uint32 { SunShadows, LampShadows, GBuffer, Lighting, Forward, Composite, Count };

[[nodiscard]] inline const char* gpuPassName(GpuPass p) noexcept
{
    switch (p)
    {
    case GpuPass::SunShadows: return "sun shadows";
    case GpuPass::LampShadows: return "lamp shadows";
    case GpuPass::GBuffer: return "g-buffer";
    case GpuPass::Lighting: return "lighting";
    case GpuPass::Forward: return "forward";
    case GpuPass::Composite: return "composite";
    case GpuPass::Count: break;
    }
    return "?";
}

struct GpuTimings {
    bool valid = false; // false when the device has no timestamps
    float32 totalMs = 0.0f;
    std::array<float32, static_cast<std::size_t>(GpuPass::Count)> passMs{};
};

struct FrameStats {
    GpuTimings gpu;
    uint32 draws = 0u;        // meshes in the draw list (before culling)
    uint32 lights = 0u;       // local lights
    uint32 shadowLights = 0u; // local lights with shadow maps
};

} // namespace Renderer
