// Renderer/backend/gbuffer_pass.cpp - deferred geometry pass. See gbuffer_pass.h.
#include "Renderer/backend/gbuffer_pass.h"

#include "Renderer/backend/resource_cache.h"
#include "Renderer/backend/vk_pipeline.h"

#include <array>

namespace Renderer::Backend {

namespace {

constexpr std::array<VkFormat, 4> kColorFormats{VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_A2B10G10R10_UNORM_PACK32,
                                                VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM};

} // namespace

void GBufferPass::create(const GpuContext& gpu, uint32 width, uint32 height, VkPipelineLayout sceneLayout)
{
    gpu_ = &gpu;
    width_ = width;
    height_ = height;
    layout_ = sceneLayout;
    const VkImageUsageFlags colorUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    Image* targets[4] = {&albedo_, &normal_, &material_, &light_};
    for (uint32 i = 0; i < 4; ++i)
        *targets[i] = createImage(gpu, {width, height, kColorFormats[i], colorUsage, VK_IMAGE_ASPECT_COLOR_BIT});
    depth_ = createImage(gpu, {width, height, VK_FORMAT_D32_SFLOAT,
                               VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_ASPECT_DEPTH_BIT});

    std::array<VkAttachmentDescription, 5> atts{};
    for (uint32 i = 0; i < 5; ++i)
    {
        VkAttachmentDescription& a = atts[i];
        a.format = i < 4 ? kColorFormats[i] : VK_FORMAT_D32_SFLOAT;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // every target is read by the lighting pass
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    std::array<VkAttachmentReference, 4> colorRefs{};
    for (uint32 i = 0; i < 4; ++i)
        colorRefs[i] = {i, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    const VkAttachmentReference depthRef{4, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 4;
    sub.pColorAttachments = colorRefs.data();
    sub.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency dep{};
    dep.srcSubpass = 0;
    dep.dstSubpass = VK_SUBPASS_EXTERNAL;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dep.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    VkRenderPassCreateInfo rp{};
    rp.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = static_cast<uint32>(atts.size());
    rp.pAttachments = atts.data();
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    rp.dependencyCount = 1;
    rp.pDependencies = &dep;
    vkCheck(vkCreateRenderPass(gpu.device, &rp, nullptr, &renderPass_), "vkCreateRenderPass(gbuffer)");

    const VkImageView views[5] = {albedo_.view, normal_.view, material_.view, light_.view, depth_.view};
    VkFramebufferCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fi.renderPass = renderPass_;
    fi.attachmentCount = 5;
    fi.pAttachments = views;
    fi.width = width;
    fi.height = height;
    fi.layers = 1;
    vkCheck(vkCreateFramebuffer(gpu.device, &fi, nullptr, &framebuffer_), "vkCreateFramebuffer(gbuffer)");

    PipelineDesc pd;
    pd.fragmentShader = "gbuffer.frag.spv";
    pd.renderPass = renderPass_;
    pd.layout = sceneLayout;
    pd.colorAttachments = 4u;
    pd.depthTest = pd.depthWrite = true;
    pd.vertexShader = "gbuffer.vert.spv";
    pd.vertexLayout = VertexLayout::Rigid;
    rigid_ = createPipeline(gpu.device, pd);
    pd.vertexShader = "skin.gbuffer.vert.spv";
    pd.vertexLayout = VertexLayout::Skinned;
    skinned_ = createPipeline(gpu.device, pd);
}

void GBufferPass::record(VkCommandBuffer cb, const DrawList& draws, const glm::mat4& viewProj, VkDescriptorSet frameSet)
{
    std::array<VkClearValue, 5> clears{};
    clears[1].color = {{0.5f, 0.5f, 1.0f, 0.0f}};
    clears[4].depthStencil.depth = 1.0f;
    VkRenderPassBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    bi.renderPass = renderPass_;
    bi.framebuffer = framebuffer_;
    bi.renderArea.extent = {width_, height_};
    bi.clearValueCount = static_cast<uint32>(clears.size());
    bi.pClearValues = clears.data();
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    setViewport(cb, width_, height_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 0, 1, &frameSet, 0, nullptr);

    const Frustum frustum(viewProj);
    VkPipeline bound = VK_NULL_HANDLE;
    for (const DrawItem& item : draws.items)
    {
        if (item.translucent || !frustum.visible(item))
            continue; // blended surfaces are drawn by the forward pass
        VkPipeline want = item.skinned ? skinned_ : rigid_;
        if (want != bound)
        {
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, want);
            bound = want;
        }
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, layout_, 1, 1, &item.material, 0, nullptr);
        vkCmdPushConstants(cb, layout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(DrawConstants),
                           &item.constants);
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &item.mesh->vertices.buffer, &offset);
        vkCmdBindIndexBuffer(cb, item.mesh->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cb, item.mesh->indexCount, 1, 0, 0, 0);
    }
    vkCmdEndRenderPass(cb);
}

void GBufferPass::destroy()
{
    if (!gpu_)
        return;
    const VkDevice d = gpu_->device;
    vkDestroyPipeline(d, rigid_, nullptr);
    vkDestroyPipeline(d, skinned_, nullptr);
    vkDestroyFramebuffer(d, framebuffer_, nullptr);
    vkDestroyRenderPass(d, renderPass_, nullptr);
    for (Image* img : {&albedo_, &normal_, &material_, &light_, &depth_})
        Backend::destroy(*gpu_, *img);
    gpu_ = nullptr;
}

} // namespace Renderer::Backend
