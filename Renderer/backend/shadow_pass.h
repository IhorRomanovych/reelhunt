// Renderer/backend/shadow_pass.h - cascaded sun shadow maps: one depth layer per cascade,
// drawn from the light with slope-scaled bias; cut-out (alpha-tested) casters discard their holes.
// Blended casters (glass, soft mesh) go instead into a half-size transmittance layer per cascade
// (RGBA16F: their colours multiplied, the nearest one's depth in alpha), so sunlight passes
// through them tinted.
#pragma once
#include "engine_types.h"

#include <array>
#include <vulkan/vulkan.h>

#include "Renderer/backend/draw_list.h"
#include "Renderer/backend/vk_common.h"

namespace Renderer::Backend {

class ShadowPass {
public:
    static constexpr uint32 kCascades = 3u;
    static constexpr uint32 kMapSize = 2048u;
    static constexpr uint32 kTintSize = 1024u;
    // View distances (metres) where each cascade ends; nothing casts beyond the last.
    static constexpr std::array<float32, kCascades> kSplits{10.0f, 35.0f, 120.0f};

    void create(const GpuContext& gpu, VkPipelineLayout sceneLayout);
    void destroy();

    // The cascades as a depth array (SHADER_READ_ONLY after record) and its compare sampler.
    [[nodiscard]] VkImageView arrayView() const noexcept { return map_.view; }
    [[nodiscard]] VkSampler sampler() const noexcept { return sampler_; }
    // The transmittance layers (SHADER_READ_ONLY after record) and their linear sampler.
    [[nodiscard]] VkImageView tintView() const noexcept { return tint_.view; }
    [[nodiscard]] VkSampler tintSampler() const noexcept { return tintSampler_; }

    // Draws every caster into each cascade. `frame` holds the cascade matrices.
    void record(VkCommandBuffer cb, const DrawList& draws, const FrameUniforms& frame, VkDescriptorSet frameSet,
                uint32 cascadeCount);

private:
    const GpuContext* gpu_ = nullptr;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    Image map_;
    std::array<VkImageView, kCascades> layerViews_{};
    std::array<VkFramebuffer, kCascades> framebuffers_{};
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkPipeline rigid_ = VK_NULL_HANDLE;
    VkPipeline skinned_ = VK_NULL_HANDLE;
    Image tint_;
    std::array<VkImageView, kCascades> tintViews_{};
    std::array<VkFramebuffer, kCascades> tintFramebuffers_{};
    VkRenderPass tintPass_ = VK_NULL_HANDLE;
    VkSampler tintSampler_ = VK_NULL_HANDLE;
    VkPipeline tintPipeline_ = VK_NULL_HANDLE;
};

} // namespace Renderer::Backend
