// Renderer/backend/gpu_timer.cpp - per-pass GPU timestamps. See gpu_timer.h.
#include "Renderer/backend/gpu_timer.h"

#include <array>
#include <iostream>
#include <vector>

namespace Renderer::Backend {

void GpuTimer::create(const GpuContext& gpu)
{
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(gpu.physical, &props);
    uint32 familyCount = 0u;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu.physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu.physical, &familyCount, families.data());
    const uint32 validBits = gpu.queueFamily < familyCount ? families[gpu.queueFamily].timestampValidBits : 0u;
    if (validBits == 0u || props.limits.timestampPeriod <= 0.0f)
    {
        std::cerr << "[Vulkan] no GPU timestamps on this queue: GPU pass times unavailable\n";
        return;
    }
    validMask_ = validBits >= 64u ? ~0ull : (1ull << validBits) - 1ull;
    nsPerTick_ = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    qi.queryType = VK_QUERY_TYPE_TIMESTAMP;
    qi.queryCount = kStamps;
    if (vkCreateQueryPool(gpu.device, &qi, nullptr, &pool_) != VK_SUCCESS)
    {
        pool_ = VK_NULL_HANDLE;
        return;
    }
    device_ = gpu.device;
}

void GpuTimer::destroy()
{
    if (pool_)
        vkDestroyQueryPool(device_, pool_, nullptr);
    pool_ = VK_NULL_HANDLE;
    recorded_ = false;
}

void GpuTimer::begin(VkCommandBuffer cb)
{
    recorded_ = false;
    if (!pool_)
        return;
    vkCmdResetQueryPool(cb, pool_, 0u, kStamps);
    vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool_, 0u);
    recorded_ = true;
}

void GpuTimer::mark(VkCommandBuffer cb, GpuPass pass)
{
    if (recorded_)
        vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool_, static_cast<uint32>(pass) + 1u);
}

void GpuTimer::collect()
{
    if (!recorded_)
        return;
    recorded_ = false;
    std::array<uint64, kStamps> ticks{};
    if (vkGetQueryPoolResults(device_, pool_, 0u, kStamps, sizeof(ticks), ticks.data(), sizeof(uint64),
                              VK_QUERY_RESULT_64_BIT) != VK_SUCCESS)
        return;
    auto ms = [&](uint64 from, uint64 to) {
        return static_cast<float32>(static_cast<double>((to - from) & validMask_) * nsPerTick_ * 1e-6);
    };
    for (uint32 i = 0; i + 1u < kStamps; ++i)
        last_.passMs[i] = ms(ticks[i], ticks[i + 1u]);
    last_.totalMs = ms(ticks[0], ticks[kStamps - 1u]);
    last_.valid = true;
}

} // namespace Renderer::Backend
