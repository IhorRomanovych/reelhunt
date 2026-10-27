// Renderer/backend/vk_pipeline.cpp - pipeline builders. See vk_pipeline.h.
#include "Renderer/backend/vk_pipeline.h"

#include "Renderer/backend/vk_common.h"

#include <array>
#include <vector>

namespace Renderer::Backend {

namespace {

struct VertexInput {
    VkVertexInputBindingDescription binding{};
    std::vector<VkVertexInputAttributeDescription> attributes;
};

VertexInput vertexInput(VertexLayout layout)
{
    VertexInput in;
    switch (layout)
    {
    case VertexLayout::None:
        break;
    case VertexLayout::Rigid:
    case VertexLayout::Skinned:
        in.binding = {0u, layout == VertexLayout::Rigid ? kRigidStride : kSkinnedStride, VK_VERTEX_INPUT_RATE_VERTEX};
        in.attributes = {{0u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 0u},
                         {1u, 0u, VK_FORMAT_R32G32B32_SFLOAT, 12u},
                         {2u, 0u, VK_FORMAT_R32G32_SFLOAT, 24u},
                         {3u, 0u, VK_FORMAT_R8G8B8A8_UNORM, 32u}};
        if (layout == VertexLayout::Skinned)
        {
            in.attributes.push_back({4u, 0u, VK_FORMAT_R8G8B8A8_UINT, 36u});
            in.attributes.push_back({5u, 0u, VK_FORMAT_R32G32B32A32_SFLOAT, 40u});
        }
        break;
    case VertexLayout::Ui:
        in.binding = {0u, kUiStride, VK_VERTEX_INPUT_RATE_VERTEX};
        in.attributes = {{0u, 0u, VK_FORMAT_R32G32_SFLOAT, 0u},
                         {1u, 0u, VK_FORMAT_R32G32_SFLOAT, 8u},
                         {2u, 0u, VK_FORMAT_R8G8B8A8_UNORM, 16u}};
        break;
    }
    return in;
}

} // namespace

VkPipeline createPipeline(VkDevice device, const PipelineDesc& desc)
{
    VkShaderModule vert = loadShader(device, desc.vertexShader);
    VkShaderModule frag = desc.fragmentShader ? loadShader(device, desc.fragmentShader) : VK_NULL_HANDLE;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vert, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, frag, "main", nullptr};

    const VertexInput in = vertexInput(desc.vertexLayout);
    VkPipelineVertexInputStateCreateInfo vis{};
    vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (desc.vertexLayout != VertexLayout::None)
    {
        vis.vertexBindingDescriptionCount = 1u;
        vis.pVertexBindingDescriptions = &in.binding;
        vis.vertexAttributeDescriptionCount = static_cast<uint32>(in.attributes.size());
        vis.pVertexAttributeDescriptions = in.attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo ia{};
    ia.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vp{};
    vp.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    vp.viewportCount = 1u;
    vp.scissorCount = 1u;

    // Culling stays off: RW geometry is often single-sided but meant to be seen from both sides.
    VkPipelineRasterizationStateCreateInfo rs{};
    rs.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    rs.depthBiasEnable = desc.depthBias ? VK_TRUE : VK_FALSE;

    VkPipelineMultisampleStateCreateInfo ms{};
    ms.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{};
    ds.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    ds.depthTestEnable = desc.depthTest ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = desc.depthWrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    std::array<VkPipelineColorBlendAttachmentState, 8> attachments{};
    for (uint32 i = 0; i < desc.colorAttachments && i < attachments.size(); ++i)
    {
        VkPipelineColorBlendAttachmentState& a = attachments[i];
        a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (desc.blend == Blend::Transmit)
        {
            a.blendEnable = VK_TRUE;
            a.colorBlendOp = VK_BLEND_OP_ADD;
            a.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            a.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
            a.alphaBlendOp = VK_BLEND_OP_MIN;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        }
        else if (desc.blend != Blend::Opaque)
        {
            a.blendEnable = VK_TRUE;
            a.colorBlendOp = VK_BLEND_OP_ADD;
            a.alphaBlendOp = VK_BLEND_OP_ADD;
            a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            a.dstColorBlendFactor = desc.blend == Blend::Additive ? VK_BLEND_FACTOR_ONE : VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        }
    }
    VkPipelineColorBlendStateCreateInfo cb{};
    cb.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    cb.attachmentCount = desc.colorAttachments;
    cb.pAttachments = attachments.data();

    std::array<VkDynamicState, 3> dynamic{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dyn{};
    dyn.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dyn.dynamicStateCount = desc.depthBias ? 3u : 2u;
    dyn.pDynamicStates = dynamic.data();

    VkGraphicsPipelineCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pi.stageCount = frag ? 2u : 1u;
    pi.pStages = stages;
    pi.pVertexInputState = &vis;
    pi.pInputAssemblyState = &ia;
    pi.pViewportState = &vp;
    pi.pRasterizationState = &rs;
    pi.pMultisampleState = &ms;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pDynamicState = &dyn;
    pi.layout = desc.layout;
    pi.renderPass = desc.renderPass;
    VkPipeline pipeline = VK_NULL_HANDLE;
    vkCheck(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pi, nullptr, &pipeline), desc.vertexShader);
    vkDestroyShaderModule(device, vert, nullptr);
    if (frag)
        vkDestroyShaderModule(device, frag, nullptr);
    return pipeline;
}

VkDescriptorSetLayoutBinding binding(uint32 index, VkDescriptorType type, VkShaderStageFlags stages)
{
    VkDescriptorSetLayoutBinding b{};
    b.binding = index;
    b.descriptorType = type;
    b.descriptorCount = 1u;
    b.stageFlags = stages;
    return b;
}

VkDescriptorSetLayout createSetLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayoutBinding> bindings)
{
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = static_cast<uint32>(bindings.size());
    ci.pBindings = bindings.begin();
    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    vkCheck(vkCreateDescriptorSetLayout(device, &ci, nullptr, &layout), "vkCreateDescriptorSetLayout");
    return layout;
}

VkPipelineLayout createPipelineLayout(VkDevice device, std::initializer_list<VkDescriptorSetLayout> sets,
                                      uint32 pushConstantBytes)
{
    VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0u, pushConstantBytes};
    VkPipelineLayoutCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    ci.setLayoutCount = static_cast<uint32>(sets.size());
    ci.pSetLayouts = sets.begin();
    ci.pushConstantRangeCount = pushConstantBytes ? 1u : 0u;
    ci.pPushConstantRanges = &pc;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    vkCheck(vkCreatePipelineLayout(device, &ci, nullptr, &layout), "vkCreatePipelineLayout");
    return layout;
}

void setViewport(VkCommandBuffer cb, uint32 width, uint32 height)
{
    const VkViewport vp{0.0f, 0.0f, static_cast<float32>(width), static_cast<float32>(height), 0.0f, 1.0f};
    const VkRect2D sc{{0, 0}, {width, height}};
    vkCmdSetViewport(cb, 0, 1, &vp);
    vkCmdSetScissor(cb, 0, 1, &sc);
}

} // namespace Renderer::Backend
