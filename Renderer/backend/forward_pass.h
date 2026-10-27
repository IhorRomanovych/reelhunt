// Renderer/backend/forward_pass.h - the forward translucent pass: alpha-blended surfaces (glass,
// decals, soft mesh, painted shadows) drawn over the lit HDR scene after the lighting pass,
// back to front, depth-tested against the opaque scene's depth without writing it, and lit by
// the same code as the deferred pass (shading.glsl, including sun cascades and lamp shadows).
#pragma once
#include "engine_types.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "Renderer/backend/draw_list.h"
#include "Renderer/backend/vk_common.h"

namespace Renderer::Backend {

class GBufferPass;
class LightingPass;
class ShadowPass;
class LampShadowPass;

class ForwardPass {
public:
    void create(const GpuContext& gpu, uint32 width, uint32 height, VkDescriptorSetLayout frameLayout,
                VkDescriptorSetLayout materialLayout, const GBufferPass& gbuffer, const LightingPass& lighting,
                const ShadowPass& shadows, const LampShadowPass& lampShadows);
    void destroy();

    void record(VkCommandBuffer cb, const DrawList& draws, const glm::mat4& viewProj, const glm::vec3& eye,
                VkDescriptorSet frameSet);

private:
    const GpuContext* gpu_ = nullptr;
    uint32 width_ = 0u, height_ = 0u;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shadowLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet shadowSet_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline rigid_ = VK_NULL_HANDLE;
    VkPipeline skinned_ = VK_NULL_HANDLE;
    VkPipeline additive_ = VK_NULL_HANDLE; // rigid, source alpha + destination
    std::vector<std::pair<float32, const DrawItem*>> sorted_;
};

} // namespace Renderer::Backend
