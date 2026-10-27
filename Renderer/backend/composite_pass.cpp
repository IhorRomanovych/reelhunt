// Renderer/backend/composite_pass.cpp - final composite + UI. See composite_pass.h.
#include "Renderer/backend/composite_pass.h"

#include "Renderer/backend/resource_cache.h"
#include "Renderer/backend/vk_device.h"
#include "Renderer/backend/vk_pipeline.h"

#include <algorithm>
#include <cstring>

namespace Renderer::Backend {

void CompositePass::create(const DeviceContext& device, ResourceCache& cache, VkImageView scene)
{
    gpu_ = &device.gpu();
    width_ = device.width();
    height_ = device.height();
    defaultView_ = cache.defaultView();
    const VkDevice d = gpu_->device;

    VkAttachmentDescription att{};
    att.format = device.swapchainFormat();
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    const VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    // Writing the swapchain image must wait for the acquire semaphore's stage.
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments = &att;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    vkCheck(vkCreateRenderPass(d, &rp, nullptr, &renderPass_), "vkCreateRenderPass(composite)");
    createFramebuffers(device);

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = si.minFilter = VK_FILTER_LINEAR;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCheck(vkCreateSampler(d, &si, nullptr, &sampler_), "vkCreateSampler(composite)");

    // scene @0, video @1, effect state @2
    const VkShaderStageFlags frag = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayout_ = createSetLayout(d, {binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                     binding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, frag),
                                     binding(2, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frag)});
    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    vkCheck(vkCreateDescriptorPool(d, &pi, nullptr, &pool_), "vkCreateDescriptorPool(composite)");
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &setLayout_;
    vkCheck(vkAllocateDescriptorSets(d, &ai, &set_), "vkAllocateDescriptorSets(composite)");

    effects_ = createBuffer(*gpu_, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(EffectState));
    std::memset(effects_.mapped, 0, sizeof(EffectState));
    const VkDescriptorBufferInfo bufInfo{effects_.buffer, 0, sizeof(EffectState)};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set_;
    w.dstBinding = 2;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    w.pBufferInfo = &bufInfo;
    vkUpdateDescriptorSets(d, 1, &w, 0, nullptr);
    writeImage(0, scene);
    writeImage(1, defaultView_);

    layout_ = createPipelineLayout(d, {setLayout_});
    PipelineDesc pd;
    pd.vertexShader = "fullscreen.vert.spv";
    pd.fragmentShader = "composite.frag.spv";
    pd.renderPass = renderPass_;
    pd.layout = layout_;
    pipeline_ = createPipeline(d, pd);

    // 2D overlay: textured quads in the same pass, material sets from the resource cache.
    uiVertices_ = createBuffer(*gpu_, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, static_cast<VkDeviceSize>(kMaxUiQuads) * 6u * kUiStride);
    uiLayout_ = createPipelineLayout(d, {cache.materialLayout()}, sizeof(float32)); // the gamma
    pd.vertexShader = "ui.vert.spv";
    pd.fragmentShader = "ui.frag.spv";
    pd.vertexLayout = VertexLayout::Ui;
    pd.layout = uiLayout_;
    pd.blend = Blend::Alpha;
    uiAlpha_ = createPipeline(d, pd);
    pd.blend = Blend::Additive;
    uiAdditive_ = createPipeline(d, pd);
}

void CompositePass::writeImage(uint32 bindingIndex, VkImageView view)
{
    const VkDescriptorImageInfo info{sampler_, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set_;
    w.dstBinding = bindingIndex;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w.pImageInfo = &info;
    vkUpdateDescriptorSets(gpu_->device, 1, &w, 0, nullptr);
}

void CompositePass::setVideo(VkImageView view)
{
    videoActive_ = view != VK_NULL_HANDLE;
    writeImage(1, videoActive_ ? view : defaultView_);
}

void CompositePass::record(VkCommandBuffer cb, uint32 imageIndex, const ScreenEffects& e, const UiBatch& ui, ResourceCache& cache)
{
    EffectState fx{};
    fx.videoActive = videoActive_ ? 1.0f : 0.0f;
    const float32 fade[4] = {e.fade.x, e.fade.y, e.fade.z, e.fade.w};
    const float32 flash[4] = {e.flash.x, e.flash.y, e.flash.z, e.flash.w};
    std::memcpy(fx.fadeColor, fade, sizeof(fade));
    std::memcpy(fx.flashColor, flash, sizeof(flash));
    fx.vignette = e.vignette;
    fx.noise = e.noise;
    fx.grain = e.grain;
    fx.scanlines = e.scanlines;
    fx.time = e.time;
    fx.height = static_cast<float32>(height_);
    fx.gamma = e.gamma > 0.0f ? e.gamma : 1.0f;
    std::memcpy(effects_.mapped, &fx, sizeof(fx));

    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = renderPass_;
    bi.framebuffer = framebuffers_[imageIndex];
    bi.renderArea.extent = {width_, height_};
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    setViewport(cb, width_, height_);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &set_, 0, nullptr);
    vkCmdDraw(cb, 3, 1, 0, 0);
    drawUi(cb, ui, cache, e.gamma);
    vkCmdEndRenderPass(cb);
}

void CompositePass::drawUi(VkCommandBuffer cb, const UiBatch& ui, ResourceCache& cache, float32 gamma)
{
    const auto& quads = ui.quads();
    const std::size_t count = std::min<std::size_t>(quads.size(), kMaxUiQuads);
    if (count == 0)
        return;
    auto* dst = static_cast<uint8_t*>(uiVertices_.mapped);
    for (std::size_t i = 0; i < count; ++i)
    {
        const UiQuad& q = quads[i];
        const uint8_t rgba[4] = {q.r, q.g, q.b, q.a};
        float32 corners[6][4] = {{q.x0, q.y0, q.u0, q.v0}, {q.x1, q.y0, q.u1, q.v0}, {q.x1, q.y1, q.u1, q.v1},
                                 {q.x0, q.y0, q.u0, q.v0}, {q.x1, q.y1, q.u1, q.v1}, {q.x0, q.y1, q.u0, q.v1}};
        if (q.freeForm)
        {
            constexpr int kOrder[6] = {0, 1, 2, 0, 2, 3};
            for (int k = 0; k < 6; ++k)
            {
                const int c = kOrder[k];
                corners[k][0] = q.px[c];
                corners[k][1] = q.py[c];
                corners[k][2] = q.pu[c];
                corners[k][3] = q.pv[c];
            }
        }
        for (const auto& c : corners)
        {
            std::memcpy(dst, c, 16u);
            std::memcpy(dst + 16u, rgba, 4u);
            dst += kUiStride;
        }
    }
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cb, 0, 1, &uiVertices_.buffer, &offset);

    // One draw per run of quads sharing a blend mode and texture.
    for (std::size_t first = 0; first < count;)
    {
        const UiQuad& head = quads[first];
        std::size_t last = first + 1;
        while (last < count && quads[last].blend == head.blend && quads[last].texture == head.texture)
            ++last;
        const VkDescriptorSet set = cache.materialSet(head.texture);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, head.blend == UiBlend::Additive ? uiAdditive_ : uiAlpha_);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, uiLayout_, 0, 1, &set, 0, nullptr);
        const float32 g = gamma > 0.0f ? gamma : 1.0f;
        vkCmdPushConstants(cb, uiLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(g), &g);
        vkCmdDraw(cb, static_cast<uint32>((last - first) * 6u), 1, static_cast<uint32>(first * 6u), 0);
        first = last;
    }
}

void CompositePass::createFramebuffers(const DeviceContext& device)
{
    const VkDevice d = gpu_->device;
    for (VkFramebuffer fb : framebuffers_)
        vkDestroyFramebuffer(d, fb, nullptr);
    framebuffers_.clear();
    for (uint32 i = 0; i < device.imageCount(); ++i)
    {
        const VkImageView view = device.swapchainView(i);
        VkFramebufferCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fi.renderPass = renderPass_;
        fi.attachmentCount = 1;
        fi.pAttachments = &view;
        fi.width = width_;
        fi.height = height_;
        fi.layers = 1;
        VkFramebuffer fb = VK_NULL_HANDLE;
        vkCheck(vkCreateFramebuffer(d, &fi, nullptr, &fb), "vkCreateFramebuffer(composite)");
        framebuffers_.push_back(fb);
    }
    swapchainGeneration_ = device.swapchainGeneration();
}

void CompositePass::syncSwapchain(const DeviceContext& device)
{
    if (gpu_ && device.swapchainGeneration() != swapchainGeneration_)
        createFramebuffers(device);
}

void CompositePass::destroy()
{
    if (!gpu_)
        return;
    const VkDevice d = gpu_->device;
    for (VkPipeline p : {pipeline_, uiAlpha_, uiAdditive_})
        vkDestroyPipeline(d, p, nullptr);
    vkDestroyPipelineLayout(d, layout_, nullptr);
    vkDestroyPipelineLayout(d, uiLayout_, nullptr);
    Backend::destroy(*gpu_, uiVertices_);
    Backend::destroy(*gpu_, effects_);
    vkDestroyDescriptorPool(d, pool_, nullptr);
    vkDestroyDescriptorSetLayout(d, setLayout_, nullptr);
    vkDestroySampler(d, sampler_, nullptr);
    for (VkFramebuffer fb : framebuffers_)
        vkDestroyFramebuffer(d, fb, nullptr);
    framebuffers_.clear();
    vkDestroyRenderPass(d, renderPass_, nullptr);
    gpu_ = nullptr;
}

} // namespace Renderer::Backend
