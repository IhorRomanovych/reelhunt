// Renderer/backend/vk_common.cpp - shared Vulkan building blocks. See vk_common.h.
#include "Renderer/backend/vk_common.h"

#include "Core/platform.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace Renderer::Backend {

namespace {

// The SPIR-V shaders are built (and installed) next to the executable.
std::string exeDir()
{
    const std::string dir = Engine::Platform::executableDir().string();
    return dir.empty() ? dir : dir + '/';
}

uint32 pickMemoryType(VkPhysicalDevice physical, uint32 typeBits, VkMemoryPropertyFlags want)
{
    VkPhysicalDeviceMemoryProperties props{};
    vkGetPhysicalDeviceMemoryProperties(physical, &props);
    for (uint32 i = 0; i < props.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) && (props.memoryTypes[i].propertyFlags & want) == want)
            return i;
    return UINT32_MAX;
}

} // namespace

void vkCheck(VkResult r, const char* what)
{
    if (r == VK_SUCCESS)
        return;
    std::cerr << "[Vulkan] " << what << " failed (VkResult " << static_cast<int>(r) << ")\n";
    std::abort();
}

uint32 findMemoryType(const GpuContext& gpu, uint32 typeBits, VkMemoryPropertyFlags want)
{
    if (const uint32 exact = pickMemoryType(gpu.physical, typeBits, want); exact != UINT32_MAX)
        return exact;
    const uint32 any = pickMemoryType(gpu.physical, typeBits, 0u);
    return any != UINT32_MAX ? any : 0u;
}

Buffer createBuffer(const GpuContext& gpu, VkBufferUsageFlags usage, VkDeviceSize size, const void* data)
{
    Buffer b;
    b.size = size > 0 ? size : 4u; // zero-sized buffers are invalid
    VkBufferCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.usage = usage;
    ci.size = b.size;
    vkCheck(vkCreateBuffer(gpu.device, &ci, nullptr, &b.buffer), "vkCreateBuffer");
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(gpu.device, b.buffer, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    // Host-coherent so writes need no flush; every host-visible type on desktop GPUs is.
    ai.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits,
                                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    vkCheck(vkAllocateMemory(gpu.device, &ai, nullptr, &b.memory), "vkAllocateMemory(buffer)");
    vkCheck(vkBindBufferMemory(gpu.device, b.buffer, b.memory, 0), "vkBindBufferMemory");
    vkCheck(vkMapMemory(gpu.device, b.memory, 0, VK_WHOLE_SIZE, 0, &b.mapped), "vkMapMemory");
    if (data)
        std::memcpy(b.mapped, data, static_cast<std::size_t>(size));
    return b;
}

void destroy(const GpuContext& gpu, Buffer& b)
{
    if (b.memory && b.mapped)
        vkUnmapMemory(gpu.device, b.memory);
    if (b.buffer)
        vkDestroyBuffer(gpu.device, b.buffer, nullptr);
    if (b.memory)
        vkFreeMemory(gpu.device, b.memory, nullptr);
    b = Buffer{};
}

VkImageView createView(const GpuContext& gpu, VkImage image, VkFormat format, VkImageAspectFlags aspect,
                       VkImageViewType type, uint32 baseLayer, uint32 layerCount, uint32 mips)
{
    VkImageViewCreateInfo vi{};
    vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    vi.image = image;
    vi.viewType = type;
    vi.format = format;
    vi.subresourceRange.aspectMask = aspect;
    vi.subresourceRange.levelCount = mips;
    vi.subresourceRange.baseArrayLayer = baseLayer;
    vi.subresourceRange.layerCount = layerCount;
    VkImageView view = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(gpu.device, &vi, nullptr, &view), "vkCreateImageView");
    return view;
}

Image createImage(const GpuContext& gpu, const ImageDesc& desc)
{
    Image img;
    img.desc = desc;
    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = desc.format;
    ci.extent = {desc.width, desc.height, 1u};
    ci.mipLevels = desc.mips;
    ci.arrayLayers = desc.layers;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = desc.usage;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    vkCheck(vkCreateImage(gpu.device, &ci, nullptr, &img.image), "vkCreateImage");
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(gpu.device, img.image, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    vkCheck(vkAllocateMemory(gpu.device, &ai, nullptr, &img.memory), "vkAllocateMemory(image)");
    vkCheck(vkBindImageMemory(gpu.device, img.image, img.memory, 0), "vkBindImageMemory");
    img.view = createView(gpu, img.image, desc.format, desc.aspect, desc.viewType, 0u, desc.layers, desc.mips);
    return img;
}

void destroy(const GpuContext& gpu, Image& img)
{
    if (img.view)
        vkDestroyImageView(gpu.device, img.view, nullptr);
    if (img.image)
        vkDestroyImage(gpu.device, img.image, nullptr);
    if (img.memory)
        vkFreeMemory(gpu.device, img.memory, nullptr);
    img = Image{};
}

void imageBarrier(VkCommandBuffer cb, VkImage image, VkImageAspectFlags aspect, const Transition& t, uint32 baseMip,
                  uint32 mipCount, uint32 layerCount)
{
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = t.from;
    b.newLayout = t.to;
    b.srcAccessMask = t.srcAccess;
    b.dstAccessMask = t.dstAccess;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange.aspectMask = aspect;
    b.subresourceRange.baseMipLevel = baseMip;
    b.subresourceRange.levelCount = mipCount;
    b.subresourceRange.layerCount = layerCount;
    vkCmdPipelineBarrier(cb, t.srcStage, t.dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

OneShot::OneShot(const GpuContext& gpu) : gpu_(gpu)
{
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = gpu.uploadPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(gpu.device, &ai, &cmd_), "vkAllocateCommandBuffers(one-shot)");
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd_, &bi), "vkBeginCommandBuffer(one-shot)");
}

OneShot::~OneShot()
{
    vkCheck(vkEndCommandBuffer(cmd_), "vkEndCommandBuffer(one-shot)");
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    vkCheck(vkQueueSubmit(gpu_.queue, 1, &si, VK_NULL_HANDLE), "vkQueueSubmit(one-shot)");
    vkCheck(vkQueueWaitIdle(gpu_.queue), "vkQueueWaitIdle(one-shot)");
    vkFreeCommandBuffers(gpu_.device, gpu_.uploadPool, 1, &cmd_);
}

VkShaderModule loadShader(VkDevice device, const char* spvName)
{
    const std::string path = exeDir() + spvName;
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
    {
        std::cerr << "[Vulkan] cannot open shader " << path << "\n";
        std::abort();
    }
    const std::size_t n = static_cast<std::size_t>(f.tellg());
    if (n == 0 || n % 4u != 0)
    {
        std::cerr << "[Vulkan] invalid SPIR-V " << path << "\n";
        std::abort();
    }
    std::vector<uint32> words(n / 4u);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(n));
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = n;
    ci.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device, &ci, nullptr, &module), spvName);
    return module;
}

} // namespace Renderer::Backend
