// Renderer/backend/vk_common.h - small Vulkan building blocks shared by every backend module:
// error checks, memory types, buffers, images, barriers, one-shot command buffers, shaders.
#pragma once
#include "engine_types.h"

#include <vulkan/vulkan.h>

namespace Renderer::Backend {

// Aborts with `what` in the message when `r` is not VK_SUCCESS (device loss and friends are
// not recoverable for this renderer).
void vkCheck(VkResult r, const char* what);

// The handles every module needs to create and upload resources.
struct GpuContext {
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32 queueFamily = 0u;
    VkCommandPool uploadPool = VK_NULL_HANDLE; // for OneShot command buffers
    bool anisotropy = false;
    float32 maxAnisotropy = 1.0f;
};

// A memory type allowed by `typeBits` with all of `want`; falls back to any allowed type.
uint32 findMemoryType(const GpuContext& gpu, uint32 typeBits, VkMemoryPropertyFlags want);

// A host-visible, persistently mapped buffer.
struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};
// Creates the buffer and, when `data` is given, copies `size` bytes into it.
Buffer createBuffer(const GpuContext& gpu, VkBufferUsageFlags usage, VkDeviceSize size, const void* data = nullptr);
void destroy(const GpuContext& gpu, Buffer& b);

// A device-local image with one view over all its layers and mips.
struct ImageDesc {
    uint32 width = 1u;
    uint32 height = 1u;
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    uint32 layers = 1u;
    uint32 mips = 1u;
    VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
};
struct Image {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    ImageDesc desc;
};
Image createImage(const GpuContext& gpu, const ImageDesc& desc);
void destroy(const GpuContext& gpu, Image& img);
VkImageView createView(const GpuContext& gpu, VkImage image, VkFormat format, VkImageAspectFlags aspect,
                       VkImageViewType type, uint32 baseLayer, uint32 layerCount, uint32 mips = 1u);

// One image layout transition.
struct Transition {
    VkImageLayout from = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout to = VK_IMAGE_LAYOUT_UNDEFINED;
    VkAccessFlags srcAccess = 0;
    VkAccessFlags dstAccess = 0;
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
};
void imageBarrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, const Transition& t,
                  uint32 baseMip = 0u, uint32 mipCount = 1u, uint32 layerCount = 1u);

// A command buffer recorded, submitted and waited for in one scope (uploads, mip generation).
class OneShot {
public:
    explicit OneShot(const GpuContext& gpu);
    ~OneShot(); // submits and waits
    OneShot(const OneShot&) = delete;
    OneShot& operator=(const OneShot&) = delete;
    [[nodiscard]] VkCommandBuffer cmd() const noexcept { return cmd_; }

private:
    const GpuContext& gpu_;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
};

// Loads `<exe dir>/<spvName>`; aborts if missing (the build puts the shaders next to the exe).
VkShaderModule loadShader(VkDevice device, const char* spvName);

} // namespace Renderer::Backend
