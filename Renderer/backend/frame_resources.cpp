// Renderer/backend/frame_resources.cpp - per-frame descriptor set 0. See frame_resources.h.
#include "Renderer/backend/frame_resources.h"

#include "Renderer/backend/vk_pipeline.h"

#include <algorithm>
#include <cstring>

namespace Renderer::Backend {

namespace {

constexpr VkDeviceSize kInitialBones = 64u * 64u;  // 64 skins of 64 bones
constexpr VkDeviceSize kInitialLights = 64u;

void writeBuffer(VkDevice device, VkDescriptorSet set, uint32 binding, VkDescriptorType type, const Buffer& buf)
{
    const VkDescriptorBufferInfo info{buf.buffer, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet w{};
    w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.dstSet = set;
    w.dstBinding = binding;
    w.descriptorCount = 1;
    w.descriptorType = type;
    w.pBufferInfo = &info;
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
}

} // namespace

void FrameResources::create(const GpuContext& gpu)
{
    gpu_ = &gpu;
    const VkShaderStageFlags both = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    layout_ = createSetLayout(gpu.device, {binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, both),
                                           binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_VERTEX_BIT),
                                           binding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, both)}); // lights: lamp shadow vertex stage too
    const VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}, {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2}};
    VkDescriptorPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pi.maxSets = 1;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    vkCheck(vkCreateDescriptorPool(gpu.device, &pi, nullptr, &pool_), "vkCreateDescriptorPool(frame)");
    VkDescriptorSetAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.descriptorPool = pool_;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &layout_;
    vkCheck(vkAllocateDescriptorSets(gpu.device, &ai, &set_), "vkAllocateDescriptorSets(frame)");

    uniforms_ = createBuffer(gpu, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, sizeof(FrameUniforms));
    writeBuffer(gpu.device, set_, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, uniforms_);
    ensureCapacity(bones_, kInitialBones * 64u, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1);
    ensureCapacity(lights_, kInitialLights * sizeof(GpuLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2);
}

void FrameResources::ensureCapacity(Buffer& buf, VkDeviceSize bytes, VkBufferUsageFlags usage, uint32 bindingIndex)
{
    if (buf.buffer && buf.size >= bytes)
        return;
    const VkDeviceSize grown = std::max(bytes, buf.size * 2u);
    Backend::destroy(*gpu_, buf);
    buf = createBuffer(*gpu_, usage, grown);
    writeBuffer(gpu_->device, set_, bindingIndex, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, buf);
}

void FrameResources::upload(const FrameUniforms& frame, const std::vector<float>& bones, const std::vector<GpuLight>& lights)
{
    std::memcpy(uniforms_.mapped, &frame, sizeof(FrameUniforms));
    ensureCapacity(bones_, bones.size() * sizeof(float), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1);
    if (!bones.empty())
        std::memcpy(bones_.mapped, bones.data(), bones.size() * sizeof(float));
    ensureCapacity(lights_, lights.size() * sizeof(GpuLight), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2);
    if (!lights.empty())
        std::memcpy(lights_.mapped, lights.data(), lights.size() * sizeof(GpuLight));
}

void FrameResources::destroy()
{
    if (!gpu_)
        return;
    Backend::destroy(*gpu_, uniforms_);
    Backend::destroy(*gpu_, bones_);
    Backend::destroy(*gpu_, lights_);
    vkDestroyDescriptorPool(gpu_->device, pool_, nullptr);
    vkDestroyDescriptorSetLayout(gpu_->device, layout_, nullptr);
    gpu_ = nullptr;
}

} // namespace Renderer::Backend
