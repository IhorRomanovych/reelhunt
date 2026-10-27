// Renderer/backend/gbuffer_pass.h - the geometry pass of the deferred renderer.
//
// Targets (all sampled by the lighting pass):
//   0 albedo    RGBA8      texture x material colour
//   1 normal    A2B10G10R10 world normal * 0.5 + 0.5
//   2 material  RGBA8      r metallic, g roughness, b 1 = baked lighting, a unused
//   3 light     RGBA8      baked prelight (Baked meshes) or the per-object ambient (w = override)
//   depth       D32
#pragma once
#include "engine_types.h"

#include <vulkan/vulkan.h>

#include "Renderer/backend/draw_list.h"
#include "Renderer/backend/vk_common.h"

namespace Renderer::Backend {

class GBufferPass {
public:
    void create(const GpuContext& gpu, uint32 width, uint32 height, VkPipelineLayout sceneLayout);
    void destroy();

    [[nodiscard]] VkImageView albedo() const noexcept { return albedo_.view; }
    [[nodiscard]] VkImageView normal() const noexcept { return normal_.view; }
    [[nodiscard]] VkImageView material() const noexcept { return material_.view; }
    [[nodiscard]] VkImageView light() const noexcept { return light_.view; }
    [[nodiscard]] VkImageView depth() const noexcept { return depth_.view; }

    void record(VkCommandBuffer cb, const DrawList& draws, const glm::mat4& viewProj, VkDescriptorSet frameSet);

private:
    const GpuContext* gpu_ = nullptr;
    uint32 width_ = 0u, height_ = 0u;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    Image albedo_, normal_, material_, light_, depth_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkPipeline rigid_ = VK_NULL_HANDLE;
    VkPipeline skinned_ = VK_NULL_HANDLE;
};

} // namespace Renderer::Backend
