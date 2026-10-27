// Renderer/backend/frame_resources.h - descriptor set 0 of every scene pipeline: the frame
// uniforms, the bone matrices of every skinned model drawn this frame and the point lights.
// The two storage buffers grow on demand, so neither characters nor lights have a fixed cap.
#pragma once
#include "engine_types.h"

#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/backend/gpu_types.h"
#include "Renderer/backend/vk_common.h"

namespace Renderer::Backend {

class FrameResources {
public:
    void create(const GpuContext& gpu);
    void destroy();

    [[nodiscard]] VkDescriptorSetLayout layout() const noexcept { return layout_; }
    [[nodiscard]] VkDescriptorSet set() const noexcept { return set_; }

    // Upload this frame's data. Called before recording; the previous frame has completed
    // (the device waits for each frame), so the buffers can be rewritten or replaced.
    void upload(const FrameUniforms& frame, const std::vector<float>& bones, const std::vector<GpuLight>& lights);

private:
    void ensureCapacity(Buffer& buf, VkDeviceSize bytes, VkBufferUsageFlags usage, uint32 binding);

    const GpuContext* gpu_ = nullptr;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkDescriptorSet set_ = VK_NULL_HANDLE;
    Buffer uniforms_;
    Buffer bones_;
    Buffer lights_;
};

} // namespace Renderer::Backend
