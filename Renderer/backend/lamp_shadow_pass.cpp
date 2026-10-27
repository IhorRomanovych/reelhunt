// Renderer/backend/lamp_shadow_pass.cpp - lamp shadow maps. See lamp_shadow_pass.h.
#include "Renderer/backend/lamp_shadow_pass.h"

#include "Renderer/backend/resource_cache.h"
#include "Renderer/backend/vk_pipeline.h"

#include <array>

namespace Renderer::Backend {

namespace {

// Perspective depth: a slightly larger constant bias than the sun cascades, plus slope.
constexpr float32 kBiasConstant = 2.0f;
constexpr float32 kBiasSlope = 2.5f;
constexpr uint32 kLayers = kMaxShadowLights * 2u;
constexpr uint32 kTintSize = kLampShadowSize / 2u;
constexpr VkFormat kTintFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// A pane light passes through tinted (the transmittance layers), as the sun's (ShadowPass).
bool transmits(const DrawItem& item)
{
    return item.translucent && !item.additive && !item.skinned;
}

} // namespace

void LampShadowPass::create(const GpuContext& gpu, VkPipelineLayout sceneLayout)
{
    gpu_ = &gpu;
    layout_ = sceneLayout;
    map_ = createImage(gpu, {kLampShadowSize, kLampShadowSize, VK_FORMAT_D32_SFLOAT,
                             VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT,
                             kLayers, 1u, VK_IMAGE_VIEW_TYPE_2D_ARRAY});
    for (uint32 i = 0; i < kLayers; ++i)
        layerViews_.push_back(createView(gpu, map_.image, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_VIEW_TYPE_2D, i, 1u));

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_LINEAR;
    si.minFilter = VK_FILTER_LINEAR;
    si.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    si.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    si.compareEnable = VK_TRUE;
    si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    vkCheck(vkCreateSampler(gpu.device, &si, nullptr, &sampler_), "vkCreateSampler(lamp shadow)");

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
    std::array<VkSubpassDependency, 2> deps{};
    // The layer may have been sampled last frame (or earlier this frame by nothing): wait for reads.
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    deps[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = static_cast<uint32>(deps.size());
    rp.pDependencies = deps.data();
    vkCheck(vkCreateRenderPass(gpu.device, &rp, nullptr, &renderPass_), "vkCreateRenderPass(lamp shadow)");

    for (uint32 i = 0; i < kLayers; ++i)
    {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = renderPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &layerViews_[i];
        fi.width = fi.height = kLampShadowSize;
        fi.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        vkCheck(vkCreateFramebuffer(gpu.device, &fi, nullptr, &fb), "vkCreateFramebuffer(lamp shadow)");
        framebuffers_.push_back(fb);
    }

    PipelineDesc pd;
    pd.fragmentShader = "shadow.frag.spv"; // the cut-out discard is shared with the cascades
    pd.renderPass = renderPass_;
    pd.layout = sceneLayout;
    pd.colorAttachments = 0u;
    pd.depthTest = pd.depthWrite = pd.depthBias = true;
    pd.vertexShader = "lampshadow.vert.spv";
    pd.vertexLayout = VertexLayout::Rigid;
    rigid_ = createPipeline(gpu.device, pd);
    pd.vertexShader = "skin.lampshadow.vert.spv";
    pd.vertexLayout = VertexLayout::Skinned;
    skinned_ = createPipeline(gpu.device, pd);

    // The transmittance layers, one a slot: cleared white with the far depth, panes multiplied in.
    tint_ = createImage(gpu, {kTintSize, kTintSize, kTintFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT, kMaxShadowLights, 1u, VK_IMAGE_VIEW_TYPE_2D_ARRAY});
    for (uint32 i = 0; i < kMaxShadowLights; ++i)
        tintViews_.push_back(createView(gpu, tint_.image, kTintFormat, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, i, 1u));
    VkSamplerCreateInfo ti{};
    ti.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    ti.magFilter = ti.minFilter = VK_FILTER_LINEAR;
    ti.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ti.addressModeU = ti.addressModeV = ti.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    ti.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    vkCheck(vkCreateSampler(gpu.device, &ti, nullptr, &tintSampler_), "vkCreateSampler(lamp tint)");
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
    std::array<VkSubpassDependency, 2> tdeps{};
    tdeps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    tdeps[0].dstSubpass = 0;
    tdeps[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    tdeps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    tdeps[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    tdeps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    tdeps[1].srcSubpass = 0;
    tdeps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    tdeps[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    tdeps[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    tdeps[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    tdeps[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo trp{};
    trp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    trp.attachmentCount = 1;
    trp.pAttachments = &ta;
    trp.subpassCount = 1;
    trp.pSubpasses = &tsub;
    trp.dependencyCount = static_cast<uint32>(tdeps.size());
    trp.pDependencies = tdeps.data();
    vkCheck(vkCreateRenderPass(gpu.device, &trp, nullptr, &tintPass_), "vkCreateRenderPass(lamp tint)");
    for (uint32 i = 0; i < kMaxShadowLights; ++i)
    {
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = tintPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &tintViews_[i];
        fi.width = fi.height = kTintSize;
        fi.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        vkCheck(vkCreateFramebuffer(gpu.device, &fi, nullptr, &fb), "vkCreateFramebuffer(lamp tint)");
        tintFramebuffers_.push_back(fb);
    }
    PipelineDesc tp;
    tp.vertexShader = "lampshadow.vert.spv";
    tp.fragmentShader = "shadowtint.frag.spv";
    tp.vertexLayout = VertexLayout::Rigid;
    tp.renderPass = tintPass_;
    tp.layout = sceneLayout;
    tp.colorAttachments = 1u;
    tp.blend = Blend::Transmit;
    tintPipeline_ = createPipeline(gpu.device, tp);
}

void LampShadowPass::drawLayer(VkCommandBuffer cb, const DrawList& draws, VkDescriptorSet frameSet, uint32 layer,
                               int32 light, bool moving)
{
    VkClearValue clear{};
    clear.depthStencil.depth = 1.0f;
    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = renderPass_;
    bi.framebuffer = framebuffers_[layer];
    bi.renderArea.extent = {kLampShadowSize, kLampShadowSize};
    bi.clearValueCount = 1;
    bi.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    if (light >= 0)
    {
        setViewport(cb, kLampShadowSize, kLampShadowSize);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &frameSet, 0, nullptr);
        const Frustum frustum(draws.lights[static_cast<std::size_t>(light)].shadowViewProj);
        VkPipeline bound = VK_NULL_HANDLE;
        for (const DrawItem& item : draws.items)
        {
            if (!item.castsShadow || item.moving != moving || transmits(item) || !frustum.visible(item))
                continue;
            VkPipeline want = item.skinned ? skinned_ : rigid_;
            if (want != bound)
            {
                vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
                vkCmdSetDepthBias(cb, kBiasConstant, 0.0f, kBiasSlope);
                bound = want;
            }
            DrawConstants pc = item.constants;
            pc.cascade = static_cast<uint32>(light);
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

void LampShadowPass::drawTint(VkCommandBuffer cb, const DrawList& draws, VkDescriptorSet frameSet, uint32 slot, int32 light)
{
    VkClearValue clear{};
    clear.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = tintPass_;
    bi.framebuffer = tintFramebuffers_[slot];
    bi.renderArea.extent = {kTintSize, kTintSize};
    bi.clearValueCount = 1;
    bi.pClearValues = &clear;
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    if (light >= 0)
    {
        setViewport(cb, kTintSize, kTintSize);
        const Frustum frustum(draws.lights[static_cast<std::size_t>(light)].shadowViewProj);
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
            pc.cascade = static_cast<uint32>(light);
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

void LampShadowPass::record(VkCommandBuffer cb, const DrawList& draws, VkDescriptorSet frameSet)
{
    // Which light owns each slot this frame.
    std::array<int32, kMaxShadowLights> lightOf{};
    lightOf.fill(-1);
    for (std::size_t i = 0; i < draws.lights.size(); ++i)
        if (const int32 slot = static_cast<int32>(draws.lights[i].params.y); slot >= 0 && slot < static_cast<int32>(kMaxShadowLights))
            lightOf[static_cast<std::size_t>(slot)] = static_cast<int32>(i);

    // First frame: every layer cleared (so all are readable); then the static layers whenever the
    // shadowed lights or the static casters change.
    const bool redrawStatic = !initialised_ || draws.shadowSignature != staticSignature_;
    for (uint32 slot = 0; slot < kMaxShadowLights; ++slot)
    {
        const int32 light = lightOf[slot];
        if (redrawStatic)
            drawLayer(cb, draws, frameSet, slot * 2u, light, false);
        if (!initialised_ || light >= 0)
        {
            drawLayer(cb, draws, frameSet, slot * 2u + 1u, light, true);
            drawTint(cb, draws, frameSet, slot, light);
        }
    }
    initialised_ = true;
    staticSignature_ = draws.shadowSignature;
}

void LampShadowPass::destroy()
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
    tintFramebuffers_.clear();
    tintViews_.clear();
    vkDestroyRenderPass(d, tintPass_, nullptr);
    vkDestroySampler(d, tintSampler_, nullptr);
    Backend::destroy(*gpu_, tint_);
    for (VkFramebuffer fb : framebuffers_)
        vkDestroyFramebuffer(d, fb, nullptr);
    for (VkImageView v : layerViews_)
        vkDestroyImageView(d, v, nullptr);
    framebuffers_.clear();
    layerViews_.clear();
    vkDestroyRenderPass(d, renderPass_, nullptr);
    vkDestroySampler(d, sampler_, nullptr);
    Backend::destroy(*gpu_, map_);
    gpu_ = nullptr;
    initialised_ = false;
}

} // namespace Renderer::Backend
