// Renderer/backend/shadow_pass.cpp - cascaded shadow maps. See shadow_pass.h.
#include "Renderer/backend/shadow_pass.h"

#include "Renderer/backend/resource_cache.h"
#include "Renderer/backend/vk_pipeline.h"

namespace Renderer::Backend {

namespace {

// Raster depth bias: a constant floor plus a term that grows with the receiver's slope, which
// together stop surfaces shadowing themselves ("acne") without lifting the shadow off its caster.
constexpr float32 kBiasConstant = 1.5f;
constexpr float32 kBiasSlope = 2.0f;

// A caster light passes through tinted (the transmittance layers) rather than one that blocks it.
bool transmits(const DrawItem& item)
{
    return item.translucent && !item.additive && !item.skinned;
}

} // namespace

void ShadowPass::create(const GpuContext& gpu, VkPipelineLayout sceneLayout)
{
    gpu_ = &gpu;
    layout_ = sceneLayout;
    map_ = createImage(gpu, {kMapSize, kMapSize, VK_FORMAT_D32_SFLOAT,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                             kCascades, 1u, VK_IMAGE_VIEW_TYPE_2D_ARRAY});
    for (uint32 i = 0; i < kCascades; ++i)
        layerViews_[i] = createView(gpu, map_.image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, i, 1u);

    // Hardware comparison with linear filtering: each tap is a 2x2 bilinear PCF. Outside the map
    // (border) counts as lit.
    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    vkCheck(vkCreateSampler(gpu.device, &si, nullptr, &sampler_), "vkCreateSampler(shadow)");

    VkAttachmentDescription att{};
    att.format = VK_FORMAT_D32_SFLOAT;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.pDepthStencilAttachment = &ref;
    // The lighting pass samples the map after this pass writes it.
    VkSubpassDependency dep{};
    dep.srcSubpass = 0;
    dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    vkCheck(vkCreateRenderPass(gpu.device, &rp, nullptr, &renderPass_), "vkCreateRenderPass(shadow)");

    for (uint32 i = 0; i < kCascades; ++i)
    {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = renderPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &layerViews_[i];
        fi.width = fi.height = kMapSize;
        fi.layers = 1;
        vkCheck(vkCreateFramebuffer(gpu.device, &fi, nullptr, &framebuffers_[i]), "vkCreateFramebuffer(shadow)");
    }

    PipelineDesc pd;
    pd.fragmentShader = "shadow.frag.spv";
    pd.renderPass = renderPass_;
    pd.layout = sceneLayout;
    pd.colorAttachments = 0u;
    pd.depthTest = pd.depthWrite = pd.depthBias = true;
    pd.vertexShader = "shadow.vert.spv";
    pd.vertexLayout = VertexLayout::Rigid;
    rigid_ = createPipeline(gpu.device, pd);
    pd.vertexShader = "skin.shadow.vert.spv";
    pd.vertexLayout = VertexLayout::Skinned;
    skinned_ = createPipeline(gpu.device, pd);

    // The transmittance layers: cleared white with the far depth, panes multiplied in.
    constexpr VkFormat kTintFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    tint_ = createImage(gpu, {kTintSize, kTintSize, kTintFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, kCascades, 1u, VK_IMAGE_VIEW_TYPE_2D_ARRAY});
    for (uint32 i = 0; i < kCascades; ++i)
        tintViews_[i] = createView(gpu, tint_.image, kTintFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, i, 1u);
    VkSamplerCreateInfo ti{};
    ti.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ti.magFilter = ti.minFilter = VK_FILTER_LINEAR;
    ti.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ti.addressModeU = ti.addressModeV = ti.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    ti.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCheck(vkCreateSampler(gpu.device, &ti, nullptr, &tintSampler_), "vkCreateSampler(shadow tint)");

    VkAttachmentDescription ta{};
    ta.format = kTintFormat;
    ta.samples = VK_SAMPLE_COUNT_1_BIT;
    ta.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    ta.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    ta.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ta.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ta.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ta.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    const VkAttachmentReference tref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription tsub{};
    tsub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    tsub.colorAttachmentCount = 1;
    tsub.pColorAttachments = &tref;
    VkSubpassDependency tdep{};
    tdep.srcSubpass = 0;
    tdep.dstSubpass = VK_SUBPASS_EXTERNAL;
    tdep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    tdep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    tdep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    tdep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo trp{};
    trp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    trp.attachmentCount = 1;
    trp.pAttachments = &ta;
    trp.subpassCount = 1;
    trp.pSubpasses = &tsub;
    trp.dependencyCount = 1;
    trp.pDependencies = &tdep;
    vkCheck(vkCreateRenderPass(gpu.device, &trp, nullptr, &tintPass_), "vkCreateRenderPass(shadow tint)");
    for (uint32 i = 0; i < kCascades; ++i)
    {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = tintPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &tintViews_[i];
        fi.width = fi.height = kTintSize;
        fi.layers = 1;
        vkCheck(vkCreateFramebuffer(gpu.device, &fi, nullptr, &tintFramebuffers_[i]), "vkCreateFramebuffer(shadow tint)");
    }
    PipelineDesc tp;
    tp.vertexShader = "shadow.vert.spv";
    tp.fragmentShader = "shadowtint.frag.spv";
    tp.vertexLayout = VertexLayout::Rigid;
    tp.renderPass = tintPass_;
    tp.layout = sceneLayout;
    tp.colorAttachments = 1u;
    tp.blend = Blend::Transmit;
    tintPipeline_ = createPipeline(gpu.device, tp);
}

void ShadowPass::record(VkCommandBuffer cb, const DrawList& draws, const FrameUniforms& frame, VkDescriptorSet frameSet,
                        uint32 cascadeCount)
{
    // Every layer is written (empty when unused) so the whole array is in SHADER_READ_ONLY.
    for (uint32 c = 0; c < kCascades; ++c)
    {
        VkClearValue clear{};
        clear.depthStencil.depth = 1.0f;
        VkRenderPassBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bi.renderPass = renderPass_;
        bi.framebuffer = framebuffers_[c];
        bi.renderArea.extent = {kMapSize, kMapSize};
        bi.clearValueCount = 1;
        bi.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
        if (c < cascadeCount)
        {
            setViewport(cb, kMapSize, kMapSize);
            vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &frameSet, 0, nullptr);
            const Frustum frustum(frame.cascadeViewProj[c]);
            VkPipeline bound = VK_NULL_HANDLE;
            for (const DrawItem& item : draws.items)
            {
                if (!item.castsShadow || transmits(item) || !frustum.visible(item))
                    continue;
                VkPipeline want = item.skinned ? skinned_ : rigid_;
                if (want != bound)
                {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
                    vkCmdSetDepthBias(cb, kBiasConstant, 0.0f, kBiasSlope);
                    bound = want;
                }
                DrawConstants pc = item.constants;
                pc.cascade = c;
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &item.material, 0, nullptr);
                vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &item.mesh->vertices.buffer, &offset);
                vkCmdBindIndexBuffer(cb, item.mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, item.mesh->indexCount, 1, 0, 0, 0);
            }
        }
        vkCmdEndRenderPass(cb);
    }

    // The panes into each cascade's transmittance layer (every layer written, as above).
    for (uint32 c = 0; c < kCascades; ++c)
    {
        VkClearValue clear{};
        clear.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
        VkRenderPassBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        bi.renderPass = tintPass_;
        bi.framebuffer = tintFramebuffers_[c];
        bi.renderArea.extent = {kTintSize, kTintSize};
        bi.clearValueCount = 1;
        bi.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
        if (c < cascadeCount)
        {
            setViewport(cb, kTintSize, kTintSize);
            const Frustum frustum(frame.cascadeViewProj[c]);
            bool bound = false;
            for (const DrawItem& item : draws.items)
            {
                if (!item.castsShadow || !transmits(item) || !frustum.visible(item))
                    continue;
                if (!bound)
                {
                    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, tintPipeline_);
                    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &frameSet, 0, nullptr);
                    bound = true;
                }
                DrawConstants pc = item.constants;
                pc.cascade = c;
                vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &item.material, 0, nullptr);
                vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(pc), &pc);
                const VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cb, 0, 1, &item.mesh->vertices.buffer, &offset);
                vkCmdBindIndexBuffer(cb, item.mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cb, item.mesh->indexCount, 1, 0, 0, 0);
            }
        }
        vkCmdEndRenderPass(cb);
    }
}

void ShadowPass::destroy()
{
    if (!gpu_)
        return;
    const VkDevice d = gpu_->device;
    vkDestroyPipeline(d, rigid_, nullptr);
    vkDestroyPipeline(d, skinned_, nullptr);
    vkDestroyPipeline(d, tintPipeline_, nullptr);
    for (VkFramebuffer fb : tintFramebuffers_)
        vkDestroyFramebuffer(d, fb, nullptr);
    for (VkImageView v : tintViews_)
        vkDestroyImageView(d, v, nullptr);
    vkDestroyRenderPass(d, tintPass_, nullptr);
    vkDestroySampler(d, tintSampler_, nullptr);
    Backend::destroy(*gpu_, tint_);
    for (VkFramebuffer fb : framebuffers_)
        vkDestroyFramebuffer(d, fb, nullptr);
    for (VkImageView v : layerViews_)
        vkDestroyImageView(d, v, nullptr);
    vkDestroyRenderPass(d, renderPass_, nullptr);
    vkDestroySampler(d, sampler_, nullptr);
    Backend::destroy(*gpu_, map_);
    gpu_ = nullptr;
}

} // namespace Renderer::Backend
