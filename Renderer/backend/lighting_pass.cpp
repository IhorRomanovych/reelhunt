// Renderer/backend/lighting_pass.cpp - deferred lighting. See lighting_pass.h.
#include "Renderer/backend/lighting_pass.h"

#include "Renderer/backend/gbuffer_pass.h"
#include "Renderer/backend/lamp_shadow_pass.h"
#include "Renderer/backend/shadow_pass.h"
#include "Renderer/backend/vk_pipeline.h"

#include <array>

namespace Renderer::Backend {

void LightingPass::create(const GpuContext& gpu, uint32 width, uint32 height, VkDescriptorSetLayout frameLayout,
                          const GBufferPass& gbuffer, const ShadowPass& shadows, const LampShadowPass& lampShadows)
{
    gpu_ = &gpu;
    width_ = width;
    height_ = height;
    target_ = createImage(gpu, {width, height, VK_FORMAT_R16G16B16A16_SFLOAT,
                                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_COLOR_BIT});

    VkAttachmentDescription att{};
    att.format = VK_FORMAT_R16G16B16A16_SFLOAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; // the fullscreen triangle writes every pixel
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkSubpassDependency dep{};
    dep.srcSubpass = 0;
    dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    vkCheck(vkCreateRenderPass(gpu.device, &rp, nullptr, &renderPass_), "vkCreateRenderPass(lighting)");
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = renderPass_;
    fi.attachmentCount = 1;
    fi.pAttachments = &target_.view;
    fi.width = width;
    fi.height = height;
    fi.layers = 1;
    vkCheck(vkCreateFramebuffer(gpu.device, &fi, nullptr, &framebuffer_), "vkCreateFramebuffer(lighting)");

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCheck(vkCreateSampler(gpu.device, &si, nullptr, &pointSampler_), "vkCreateSampler(gbuffer)");

    // set 1: albedo, normal, material, light, depth (G-buffer), the sun cascades, the lamp shadows,
    // the sun's transmittance.
    const VkShaderStageFlags frag = VK_SHADER_STAGE_FRAGMENT_BIT;
    inputLayout_ = createSetLayout(gpu.device, {binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                                binding(8, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag)});
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 9};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    vkCheck(vkCreateDescriptorPool(gpu.device, &pi, nullptr, &pool_), "vkCreateDescriptorPool(lighting)");
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &inputLayout_;
    vkCheck(vkAllocateDescriptorSets(gpu.device, &ai, &inputSet_), "vkAllocateDescriptorSets(lighting)");

    const std::array<VkDescriptorImageInfo, 9> infos{{
        {pointSampler_, gbuffer.albedo(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {pointSampler_, gbuffer.normal(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {pointSampler_, gbuffer.material(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {pointSampler_, gbuffer.light(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {pointSampler_, gbuffer.depth(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {shadows.sampler(), shadows.arrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {lampShadows.sampler(), lampShadows.arrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {shadows.tintSampler(), shadows.tintView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {lampShadows.tintSampler(), lampShadows.tintView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    }};
    std::array<VkWriteDescriptorSet, 9> writes{};
    for (uint32 i = 0; i < 9; ++i)
    {
        writes[i] = {};
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = inputSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(gpu.device, static_cast<uint32>(writes.size()), writes.data(), 0, nullptr);

    layout_ = createPipelineLayout(gpu.device, {frameLayout, inputLayout_});
    PipelineDesc pd;
    pd.vertexShader = "fullscreen.vert.spv";
    pd.fragmentShader = "light.frag.spv";
    pd.renderPass = renderPass_;
    pd.layout = layout_;
    pipeline_ = createPipeline(gpu.device, pd);
}

void LightingPass::record(VkCommandBuffer cb, VkDescriptorSet frameSet)
{
    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = renderPass_;
    bi.framebuffer = framebuffer_;
    bi.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    setViewport(cb, width_, height_);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    const VkDescriptorSet sets[2] = {frameSet, inputSet_};
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 2, sets, 0, nullptr);
    vkCmdDraw(cb, 3, 1, 0, 0);
    vkCmdEndRenderPass(cb);
}

void LightingPass::destroy()
{
    if (!gpu_)
        return;
    const VkDevice d = gpu_->device;
    vkDestroyPipeline(d, pipeline_, nullptr);
    vkDestroyPipelineLayout(d, layout_, nullptr);
    vkDestroyDescriptorPool(d, pool_, nullptr);
    vkDestroyDescriptorSetLayout(d, inputLayout_, nullptr);
    vkDestroySampler(d, pointSampler_, nullptr);
    vkDestroyFramebuffer(d, framebuffer_, nullptr);
    vkDestroyRenderPass(d, renderPass_, nullptr);
    Backend::destroy(*gpu_, target_);
    gpu_ = nullptr;
}

} // namespace Renderer::Backend
