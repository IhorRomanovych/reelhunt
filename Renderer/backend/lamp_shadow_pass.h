// Renderer/backend/lamp_shadow_pass.h - shadow maps for the shadow-casting spot lights (lamps).
//
// Each shadowed light (slot s, up to kMaxShadowLights) owns two layers of one depth array:
//   2s     the static scene (world, props, fences through their holes) - drawn once and kept
//          until a shadowed light or the set of static casters changes (DrawList::shadowSignature)
//   2s + 1 moving casters (characters, movable props) - redrawn every frame
// The lighting uses both (shading.glsl): characters are shadowed by the static scene and by each
// other; baked surfaces lose a lamp's light only where a moving caster blocks what the static
// scene lets through.
// Panes (glass, soft mesh) cast no depth: each slot has a layer of a transmittance array instead,
// redrawn every frame as the sun's (ShadowPass): the panes' colours multiplied, the nearest one's
// depth in alpha. Lamp light reaching a point behind a pane takes its colour.
#pragma once
#include "engine_types.h"

#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/backend/draw_list.h"
#include "Renderer/backend/vk_common.h"

namespace Renderer::Backend {

class LampShadowPass {
public:
    void create(const GpuContext& gpu, VkPipelineLayout sceneLayout);
    void destroy();

    [[nodiscard]] VkImageView arrayView() const noexcept { return map_.view; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    [[nodiscard]] VkImageView tintView() const noexcept { return tint_.view; }
    [[nodiscard]] VkSampler tintSampler() const noexcept { return tintSampler_; }

    void record(VkCommandBuffer cb, const DrawList& draws, VkDescriptorSet frameSet);

private:
    void drawLayer(VkCommandBuffer cb, const DrawList& draws, VkDescriptorSet frameSet, uint32 layer, int32 light, bool moving);
    void drawTint(VkCommandBuffer cb, const DrawList& draws, VkDescriptorSet frameSet, uint32 slot, int32 light);

    const GpuContext* gpu_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    Image map_;
    std::vector<VkImageView> layerViews_;
    std::vector<VkFramebuffer> framebuffers_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipeline rigid_ = VK_NULL_HANDLE;
    VkPipeline skinned_ = VK_NULL_HANDLE;
    Image tint_;
    std::vector<VkImageView> tintViews_;
    std::vector<VkFramebuffer> tintFramebuffers_;
    VkRenderPass tintPass_ = VK_NULL_HANDLE;
    VkSampler tintSampler_ = VK_NULL_HANDLE;
    VkPipeline tintPipeline_ = VK_NULL_HANDLE;
    bool initialised_ = false;
    uint64 staticSignature_ = 0u;
};

} // namespace Renderer::Backend
