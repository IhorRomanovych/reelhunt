// Renderer/backend/lighting_pass.h - the deferred lighting pass: reads the G-buffer and the
// shadow cascades, writes the lit scene into an HDR (RGBA16F) target for the composite pass.
#pragma once
#include "engine_types.h"

#include <vulkan/vulkan.h>

#include "Renderer/backend/vk_common.h"

namespace Renderer::Backend {

class GBufferPass;
class ShadowPass;
class LampShadowPass;

class LightingPass {
public:
    void create(const GpuContext& gpu, uint32 width, uint32 height, VkDescriptorSetLayout frameLayout,
                const GBufferPass& gbuffer, const ShadowPass& shadows, const LampShadowPass& lampShadows);
    void destroy();

    [[nodiscard]] VkImageView output() const noexcept { return target_.view; }

    void record(VkCommandBuffer cb, VkDescriptorSet frameSet);

private:
    const GpuContext* gpu_ = nullptr;
    uint32 width_ = 0u, height_ = 0u;
    Image target_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkSampler pointSampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout inputLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet inputSet_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

} // namespace Renderer::Backend
