// Renderer/backend/composite_pass.h - the final pass into the swapchain image: the lit scene with
// screen effects (fade, flash, vignette) and the Bink video overlay, then the 2D UI on top.
#pragma once
#include "engine_types.h"

#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/backend/vk_common.h"
#include "Renderer/screen_effects.h"
#include "Renderer/ui.h"

namespace Renderer::Backend {

class DeviceContext;
class ResourceCache;

// Screen-effect state (mirrors composite.frag.glsl).
struct EffectState {
    float32 fadeColor[4]{};   // rgb, a = opacity
    float32 flashColor[4]{};  // rgb, a = intensity
    float32 vignette = 0.0f;
    float32 videoActive = 0.0f;
    float32 noise = 0.0f;     // white noise 0..1
    float32 grain = 0.0f;     // film grain 0..1
    float32 scanlines = 0.0f; // interlace lines 0..1
    float32 time = 0.0f;      // seconds
    float32 height = 0.0f;    // target height in pixels (scanline spacing)
    float32 gamma = 1.0f;     // the final colour's exponent (brightness)
};

class CompositePass {
public:
    void create(const DeviceContext& device, ResourceCache& cache, VkImageView scene);
    void destroy();

    // Show `view` as the video overlay (nullptr hides it).
    void setVideo(VkImageView view);

    void record(VkCommandBuffer cb, uint32 imageIndex, const ScreenEffects& effects, const UiBatch& ui, ResourceCache& cache);
    // Rebuilds the per-image framebuffers when the device rebuilt its swapchain.
    void syncSwapchain(const DeviceContext& device);

private:
    void writeImage(uint32 binding, VkImageView view);
    void createFramebuffers(const DeviceContext& device);
    void drawUi(VkCommandBuffer cb, const UiBatch& ui, ResourceCache& cache, float32 gamma);

    static constexpr uint32 kMaxUiQuads = 8192u;

    const GpuContext* gpu_ = nullptr;
    uint32 width_ = 0u, height_ = 0u;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_; // one per swapchain image
    uint32 swapchainGeneration_ = 0u;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkImageView defaultView_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    Buffer effects_;
    bool videoActive_ = false;

    VkPipelineLayout uiLayout_ = VK_NULL_HANDLE;
    VkPipeline uiAlpha_ = VK_NULL_HANDLE;
    VkPipeline uiAdditive_ = VK_NULL_HANDLE;
    Buffer uiVertices_;
};

} // namespace Renderer::Backend
