// Renderer/backend/gpu_timer.h - the GPU time each pass of a frame takes (timestamp queries).
//
// begin() resets the query pool and stamps the start of the command buffer, mark(i) stamps
// the end of pass i, collect() reads the stamps back once the frame's fence has signalled (the
// device runs one frame in flight, so that is right after submitAndPresent()).
#pragma once
#include "engine_types.h"

#include <vulkan/vulkan.h>

#include "Renderer/backend/vk_common.h"
#include "Renderer/frame_stats.h"

namespace Renderer::Backend {


class GpuTimer {
public:
    void create(const GpuContext& gpu);
    void destroy();

    void begin(VkCommandBuffer cb);
    void mark(VkCommandBuffer cb, GpuPass pass);
    // Reads the frame recorded since begin(); call after its fence has signalled.
    void collect();

    [[nodiscard]] const GpuTimings& last() const noexcept { return last_; }

private:
    static constexpr uint32 kStamps = static_cast<uint32>(GpuPass::Count) + 1u;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    double nsPerTick_ = 1.0;
    uint64 validMask_ = 0u;
    bool recorded_ = false;
    GpuTimings last_;
};

} // namespace Renderer::Backend
