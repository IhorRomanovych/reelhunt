// Renderer/backend/forward_pass.cpp - forward translucent pass. See forward_pass.h.
#include "Renderer/backend/forward_pass.h"

#include "Renderer/backend/gbuffer_pass.h"
#include "Renderer/backend/lamp_shadow_pass.h"
#include "Renderer/backend/lighting_pass.h"
#include "Renderer/backend/resource_cache.h"
#include "Renderer/backend/shadow_pass.h"
#include "Renderer/backend/vk_pipeline.h"

#include <algorithm>
#include <array>

namespace Renderer::Backend {

namespace {

// Decals lie on (or a hair above) the surface they mark: pull them toward the eye a little.
constexpr float32 kDecalBiasConstant = -2.0f;
constexpr float32 kDecalBiasSlope = -1.5f;

} // namespace

void ForwardPass::create(const GpuContext& gpu, uint32 width, uint32 height, VkDescriptorSetLayout frameLayout,
                         VkDescriptorSetLayout materialLayout, const GBufferPass& gbuffer, const LightingPass& lighting,
                         const ShadowPass& shadows, const LampShadowPass& lampShadows)
{
    gpu_ = &gpu;
    width_ = width;
    height_ = height;
    const VkDevice d = gpu.device;

    // Colour: the lit HDR scene (kept, blended onto). Depth: the G-buffer's, read-only.
    std::array<VkAttachmentDescription, 2> atts{};
    atts[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
    atts[0].samples = VK_SAMPLE_COUNT_1_BIT;
    atts[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    atts[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    atts[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    atts[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    atts[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    atts[1] = atts[0];
    atts[1].format = VK_FORMAT_D32_SFLOAT;
    const VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;
    std::array<VkSubpassDependency, 2> deps{};
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                           VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = static_cast<uint32>(atts.size());
    rp.pAttachments = atts.data();
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = static_cast<uint32>(deps.size());
    rp.pDependencies = deps.data();
    vkCheck(vkCreateRenderPass(d, &rp, nullptr, &renderPass_), "vkCreateRenderPass(forward)");

    const VkImageView views[2] = {lighting.output(), gbuffer.depth()};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = renderPass_;
    fi.attachmentCount = 2;
    fi.pAttachments = views;
    fi.width = width;
    fi.height = height;
    fi.layers = 1;
    vkCheck(vkCreateFramebuffer(d, &fi, nullptr, &framebuffer_), "vkCreateFramebuffer(forward)");

    // set 2: the sun cascades, the lamp shadows, the sun's and the lamps' transmittance.
    const VkShaderStageFlags frag = VK_SHADER_STAGE_FRAGMENT_BIT;
    shadowLayout_ = createSetLayout(d, {binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                        binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                        binding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                        binding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag)});
    const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = 1;
    pi.pPoolSizes = &size;
    vkCheck(vkCreateDescriptorPool(d, &pi, nullptr, &pool_), "vkCreateDescriptorPool(forward)");
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &shadowLayout_;
    vkCheck(vkAllocateDescriptorSets(d, &ai, &shadowSet_), "vkAllocateDescriptorSets(forward)");
    const std::array<VkDescriptorImageInfo, 4> infos{{
        {shadows.sampler(), shadows.arrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {lampShadows.sampler(), lampShadows.arrayView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {shadows.tintSampler(), shadows.tintView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
        {lampShadows.tintSampler(), lampShadows.tintView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
    }};
    std::array<VkWriteDescriptorSet, 4> writes{};
    for (uint32 i = 0; i < 4; ++i)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = shadowSet_;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i].pImageInfo = &infos[i];
    }
    vkUpdateDescriptorSets(d, static_cast<uint32>(writes.size()), writes.data(), 0, nullptr);

    layout_ = createPipelineLayout(d, {frameLayout, materialLayout, shadowLayout_}, sizeof(DrawConstants));
    PipelineDesc pd;
    pd.fragmentShader = "forward.frag.spv";
    pd.renderPass = renderPass_;
    pd.layout = layout_;
    pd.colorAttachments = 1u;
    pd.blend = Blend::Alpha;
    pd.depthTest = true;
    pd.depthWrite = false;
    pd.depthBias = true;
    pd.vertexShader = "gbuffer.vert.spv";
    pd.vertexLayout = VertexLayout::Rigid;
    rigid_ = createPipeline(d, pd);
    pd.vertexShader = "skin.gbuffer.vert.spv";
    pd.vertexLayout = VertexLayout::Skinned;
    skinned_ = createPipeline(d, pd);
    pd.vertexShader = "gbuffer.vert.spv";
    pd.vertexLayout = VertexLayout::Rigid;
    pd.blend = Blend::Additive;
    additive_ = createPipeline(d, pd);
}

void ForwardPass::record(VkCommandBuffer cb, const DrawList& draws, const glm::mat4& viewProj, const glm::vec3& eye,
                         VkDescriptorSet frameSet)
{
    // Back to front by bounding-sphere centre (unbounded items last, i.e. drawn as nearest).
    const Frustum frustum(viewProj);
    sorted_.clear();
    for (const DrawItem& item : draws.items)
        if (item.translucent && frustum.visible(item))
            sorted_.emplace_back(item.radius < 0.0f ? 0.0f : glm::length(item.center - eye), &item);
    std::sort(sorted_.begin(), sorted_.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = renderPass_;
    bi.framebuffer = framebuffer_;
    bi.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    if (!sorted_.empty())
    {
        setViewport(cb, width_, height_);
        const VkDescriptorSet sets[1] = {frameSet};
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, sets, 0, nullptr);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 2, 1, &shadowSet_, 0, nullptr);
        VkPipeline bound = VK_NULL_HANDLE;
        for (const auto& [distance, item] : sorted_)
        {
            VkPipeline want = item->skinned ? skinned_ : item->additive ? additive_ : rigid_;
            if (want != bound)
            {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
                vkCmdSetDepthBias(cb, kDecalBiasConstant, 0.0f, kDecalBiasSlope);
                bound = want;
            }
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &item->material, 0, nullptr);
            vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawConstants),
                               &item->constants);
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(cb, 0, 1, &item->mesh->vertices.buffer, &offset);
            vkCmdBindIndexBuffer(cb, item->mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(cb, item->mesh->indexCount, 1, 0, 0, 0);
        }
    }
    vkCmdEndRenderPass(cb);
}

void ForwardPass::destroy()
{
    if (!gpu_)
        return;
    const VkDevice d = gpu_->device;
    vkDestroyPipeline(d, rigid_, nullptr);
    vkDestroyPipeline(d, skinned_, nullptr);
    vkDestroyPipeline(d, additive_, nullptr);
    vkDestroyPipelineLayout(d, layout_, nullptr);
    vkDestroyDescriptorPool(d, pool_, nullptr);
    vkDestroyDescriptorSetLayout(d, shadowLayout_, nullptr);
    vkDestroyFramebuffer(d, framebuffer_, nullptr);
    vkDestroyRenderPass(d, renderPass_, nullptr);
    gpu_ = nullptr;
}

} // namespace Renderer::Backend
