// Renderer/backend/vulkan.h - the Vulkan renderer: owns the device and the frame's passes.
//
// Frame: beginFrame() -> drawScene(scene, ui) -> present().
//   1. DrawList     scene -> draws, bone matrices of every skinned model, point lights
//   2. ShadowPass   sun / moon cascades (Midday and Dynamic lighting)
//   3. LampShadowPass spot-light shadow maps: static scene (cached) + moving casters
//   4. GBufferPass  albedo / normal / material / light / depth (opaque and cut-out surfaces)
//   5. LightingPass G-buffer + shadows -> HDR scene
//   6. ForwardPass  alpha-blended surfaces over the HDR scene, back to front
//   7. CompositePass scene + effects + video -> swapchain, then the 2D UI
// GPU copies of meshes and textures are created on first use (ResourceCache); call removeMesh /
// removeTexture / releaseScene before destroying the CPU objects.
#pragma once
#include "engine_types.h"

#include <SDL3/SDL.h>
#include <vulkan/vulkan.h>

#include "Renderer/backend/composite_pass.h"
#include "Renderer/backend/draw_list.h"
#include "Renderer/backend/forward_pass.h"
#include "Renderer/backend/gpu_timer.h"
#include "Renderer/backend/lamp_shadow_pass.h"
#include "Renderer/backend/frame_resources.h"
#include "Renderer/backend/gbuffer_pass.h"
#include "Renderer/backend/lighting_pass.h"
#include "Renderer/backend/resource_cache.h"
#include "Renderer/backend/shadow_pass.h"
#include "Renderer/backend/vk_device.h"
#include "Renderer/scene.h"
#include "Renderer/texture.h"
#include "Renderer/ui.h"

namespace Renderer::Backend {


class Vulkan {
public:
    Vulkan() noexcept = default;
    ~Vulkan() noexcept { shutdown(); }
    Vulkan(const Vulkan&) = delete;
    Vulkan& operator=(const Vulkan&) = delete;

    // Create the window and every GPU resource. Returns false if Vulkan is unavailable.
    bool create(const VulkanCreateParams& p) noexcept;
    void shutdown() noexcept;

    [[nodiscard]] bool isReady() const noexcept { return ready_; }
    [[nodiscard]] SDL_Window* window() const noexcept { return device_.window(); }
    [[nodiscard]] uint32 width() const noexcept { return device_.width(); }
    [[nodiscard]] uint32 height() const noexcept { return device_.height(); }

    void beginFrame() noexcept;
    void drawScene(const Scene& scene, const UiBatch& ui) noexcept;
    void present() noexcept;

    // Upload `tex` (a decoded video frame) and show it over the scene until clearVideo().
    void setVideoFrame(const Texture& tex) noexcept;
    void clearVideo() noexcept;

    void removeMesh(const Mesh* mesh) noexcept;
    void removeTexture(uint32 handle) noexcept;
    // Free the GPU copies of every mesh in `scene` (call before destroying the scene).
    void releaseScene(const Scene& scene) noexcept;

    [[nodiscard]] const FrameStats& frameStats() const noexcept { return stats_; }

private:
    FrameUniforms frameUniforms(const Scene& scene, uint32& cascadeCount) const;

    DeviceContext device_;
    ResourceCache cache_;
    FrameResources frame_;
    VkPipelineLayout sceneLayout_ = VK_NULL_HANDLE; // set 0 frame, set 1 material, 128 B push
    ShadowPass shadows_;
    LampShadowPass lampShadows_;
    GBufferPass gbuffer_;
    LightingPass lighting_;
    ForwardPass forward_;
    CompositePass composite_;
    DrawList draws_;
    GpuTimer timer_;
    FrameStats stats_;
    float32 debugView_ = 0.0f; // REELHUNT_DEBUG_VIEW
    uint32 videoHandle_ = 0u; // texture shown as the video overlay (0 = none)
    bool ready_ = false;
    bool frameActive_ = false;
};

} // namespace Renderer::Backend
