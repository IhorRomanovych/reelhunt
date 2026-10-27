// Renderer/backend/vk_pipeline.h - graphics pipeline, layout and descriptor-set-layout builders.
//
// Every pipeline uses a dynamic viewport and scissor (set per pass with setViewport), so the
// same pipeline serves any target size.
#pragma once
#include "engine_types.h"

#include <initializer_list>
#include <vulkan/vulkan.h>

namespace Renderer::Backend {

// Vertex streams (one interleaved binding each).
enum class VertexLayout {
    None,    // fullscreen triangle generated from gl_VertexIndex
    Rigid,   // 36 B: position f32x3, normal f32x3, uv f32x2, colour u8x4 (unorm)
    Skinned, // 56 B: Rigid + bone indices u8x4 (uint) + weights f32x4
    Ui,      // 20 B: position f32x2, uv f32x2, colour u8x4 (unorm)
};
inline constexpr uint32 kRigidStride = 36u;
inline constexpr uint32 kSkinnedStride = 56u;
inline constexpr uint32 kUiStride = 20u;

// Transmit: light through glass - the colour multiplied into the target (dst x src), the alpha
// the smaller of the two (the nearest pane's depth).
enum class Blend { Opaque, Alpha, Additive, Transmit };

struct PipelineDesc {
    const char* vertexShader = nullptr;   // SPIR-V file names next to the exe
    const char* fragmentShader = nullptr; // may be null for depth-only passes
    VertexLayout vertexLayout = VertexLayout::None;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    uint32 colorAttachments = 1u;
    Blend blend = Blend::Opaque;
    bool depthTest = false;
    bool depthWrite = false;
    bool depthBias = false; // slope-scaled bias set per draw with vkCmdSetDepthBias
};

VkPipeline createPipeline(VkDevice device, const PipelineDesc& desc);

VkDescriptorSetLayout createSetLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings);
// A binding entry for createSetLayout.
VkDescriptorSetLayoutBinding binding(uint32 index, VkDescriptorType type, VkShaderStageFlags stages);

VkPipelineLayout createPipelineLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> sets,
                                      uint32 pushConstantBytes = 0u);

// Viewport + scissor covering a w x h target.
void setViewport(VkCommandBuffer cb, uint32 width, uint32 height);

} // namespace Renderer::Backend
