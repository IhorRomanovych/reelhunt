// Renderer/backend/vk_device.h - the SDL window, Vulkan instance/device/queue, swapchain and
// the per-frame command buffer + synchronisation.
#pragma once
#include "engine_types.h"

#include <SDL3/SDL.h>
#include <vector>
#include <vulkan/vulkan.h>

#include "Renderer/backend/vk_common.h"

namespace Renderer::Backend {

struct VulkanCreateParams {
    uint32 width = 800u;
    uint32 height = 600u;
    bool enableValidation = false; // enable VK_LAYER_KHRONOS_validation when it is installed
    // Where and how the window opens (the launcher's choice, Game/launcher.h): the SDL display
    // (0 = the primary one), fullscreen in the display mode closest to width x height at
    // `refresh` Hz (0 = any), else a window centred on that display.
    uint32 display = 0u;
    bool fullscreen = false;
    float32 refresh = 0.0f;
};

// One frame in flight: acquire() begins the command buffer for the next swapchain image,
// submitAndPresent() submits it, waits for the GPU (so per-frame CPU writes to mapped buffers
// are safe without double-buffering) and presents.
class DeviceContext {
public:
    bool create(const VulkanCreateParams& p);
    void destroy();

    [[nodiscard]] const GpuContext& gpu() const noexcept { return gpu_; }
    [[nodiscard]] SDL_Window* window() const noexcept { return window_; }
    [[nodiscard]] uint32 width() const noexcept { return width_; }
    [[nodiscard]] uint32 height() const noexcept { return height_; }
    [[nodiscard]] VkFormat swapchainFormat() const noexcept { return swapchainFormat_; }
    [[nodiscard]] uint32 imageCount() const noexcept { return static_cast<uint32>(swapchainViews_.size()); }
    [[nodiscard]] VkImageView swapchainView(uint32 i) const noexcept { return swapchainViews_[i]; }
    // Bumped whenever the swapchain is rebuilt (its views change): users of the views rebuild.
    [[nodiscard]] uint32 swapchainGeneration() const noexcept { return swapchainGeneration_; }

    // False when no image could be acquired: skip recording this frame.
    bool acquire();
    [[nodiscard]] VkCommandBuffer cmd() const noexcept { return cmd_; }
    [[nodiscard]] uint32 imageIndex() const noexcept { return imageIndex_; }
    void submitAndPresent();
    void waitIdle() const;

private:
    bool createInstance(bool validation);
    bool pickPhysicalDevice();
    bool createDevice();
    bool createSwapchain(VkSwapchainKHR old = VK_NULL_HANDLE);
    // A new swapchain of the same size (after alt-tab from fullscreen, a display change ...);
    // false while the window cannot take one (minimised).
    bool rebuildSwapchain();

    GpuContext gpu_;
    SDL_Window* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkImageView> swapchainViews_;
    uint32 width_ = 0u, height_ = 0u;
    uint32 swapchainGeneration_ = 0u;
    bool swapchainStale_ = false; // present said out of date / suboptimal: rebuild before the next frame

    VkCommandPool framePool_ = VK_NULL_HANDLE;
    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    std::vector<VkSemaphore> renderFinished_; // one per swapchain image
    uint32 imageIndex_ = 0u;
};

} // namespace Renderer::Backend
