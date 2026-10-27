// Renderer/backend/vk_device.cpp - window, device, swapchain, frame sync. See vk_device.h.
#include "Renderer/backend/vk_device.h"

#include <SDL3/SDL_vulkan.h>
#include <algorithm>
#include <cstring>
#include <iostream>

namespace Renderer::Backend {

bool DeviceContext::create(const VulkanCreateParams& p)
{
    if (!SDL_Init(SDL_INIT_VIDEO) || !SDL_Vulkan_LoadLibrary(nullptr))
    {
        std::cerr << "[Vulkan] SDL init: " << SDL_GetError() << "\n";
        return false;
    }
    // The window opens on the chosen display, hidden until it has its final mode.
    const SDL_DisplayID display = p.display ? p.display : SDL_GetPrimaryDisplay();
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Reelhunt");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, static_cast<int>(p.width));
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, static_cast<int>(p.height));
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_VULKAN_BOOLEAN, true);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, true);
    window_ = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!window_)
    {
        std::cerr << "[Vulkan] SDL_CreateWindow: " << SDL_GetError() << "\n";
        return false;
    }
    if (p.fullscreen)
    {
        // Exclusive fullscreen in the display mode nearest the chosen one.
        SDL_DisplayMode mode{};
        if (SDL_GetClosestFullscreenDisplayMode(display, static_cast<int>(p.width), static_cast<int>(p.height), p.refresh,
                                                false, &mode) &&
            SDL_SetWindowFullscreenMode(window_, &mode) && SDL_SetWindowFullscreen(window_, true))
        {
            SDL_SyncWindow(window_);
            std::cerr << "[Vulkan] fullscreen " << mode.w << "x" << mode.h << " @ " << mode.refresh_rate << " Hz on display "
                      << display << "\n";
        }
        else
            std::cerr << "[Vulkan] fullscreen " << p.width << "x" << p.height << " failed (" << SDL_GetError()
                      << "): windowed\n";
    }
    SDL_ShowWindow(window_);
    SDL_SyncWindow(window_);
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    width_ = static_cast<uint32>(w);
    height_ = static_cast<uint32>(h);

    if (!createInstance(p.enableValidation) || !SDL_Vulkan_CreateSurface(window_, instance_, nullptr, &surface_) ||
        !pickPhysicalDevice() || !createDevice() || !createSwapchain())
        return false;

    VkCommandPoolCreateInfo pi{};
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = gpu_.queueFamily;
    vkCheck(vkCreateCommandPool(gpu_.device, &pi, nullptr, &framePool_), "vkCreateCommandPool(frame)");
    pi.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    vkCheck(vkCreateCommandPool(gpu_.device, &pi, nullptr, &gpu_.uploadPool), "vkCreateCommandPool(upload)");
    VkCommandBufferAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = framePool_;
    ai.commandBufferCount = 1;
    vkCheck(vkAllocateCommandBuffers(gpu_.device, &ai, &cmd_), "vkAllocateCommandBuffers(frame)");

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vkCheck(vkCreateFence(gpu_.device, &fi, nullptr, &fence_), "vkCreateFence");
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    vkCheck(vkCreateSemaphore(gpu_.device, &si, nullptr, &imageAvailable_), "vkCreateSemaphore");
    renderFinished_.resize(swapchainViews_.size());
    for (VkSemaphore& s : renderFinished_)
        vkCheck(vkCreateSemaphore(gpu_.device, &si, nullptr, &s), "vkCreateSemaphore");
    return true;
}

bool DeviceContext::createInstance(bool validation)
{
    uint32 extCount = 0;
    const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&extCount);
    VkApplicationInfo app{};
    app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    app.pApplicationName = "Reelhunt";
    app.apiVersion = VK_API_VERSION_1_2;
    VkInstanceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = extCount;
    ci.ppEnabledExtensionNames = exts;
    // Validation only on request and only if installed: a missing layer fails vkCreateInstance.
    static const char* kValidation = "VK_LAYER_KHRONOS_validation";
    if (validation)
    {
        uint32 n = 0;
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> layers(n);
        vkEnumerateInstanceLayerProperties(&n, layers.data());
        if (std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& l) { return std::strcmp(l.layerName, kValidation) == 0; }))
        {
            ci.enabledLayerCount = 1;
            ci.ppEnabledLayerNames = &kValidation;
        }
        else
            std::cerr << "[Vulkan] validation requested but " << kValidation << " is not installed\n";
    }
    if (const VkResult r = vkCreateInstance(&ci, nullptr, &instance_); r != VK_SUCCESS)
    {
        std::cerr << "[Vulkan] vkCreateInstance failed (" << static_cast<int>(r) << ")\n";
        return false;
    }
    return true;
}

bool DeviceContext::pickPhysicalDevice()
{
    uint32 n = 0;
    if (vkEnumeratePhysicalDevices(instance_, &n, nullptr) != VK_SUCCESS || n == 0)
        return false;
    std::vector<VkPhysicalDevice> devices(n);
    vkEnumeratePhysicalDevices(instance_, &n, devices.data());

    // Prefer the NVIDIA GPU (the dev machine has two); otherwise the first with a graphics queue.
    constexpr uint32 kVendorNvidia = 0x10DEu;
    auto graphicsFamily = [](VkPhysicalDevice d) -> int {
        uint32 qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qf(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qf.data());
        for (uint32 i = 0; i < qn; ++i)
            if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                return static_cast<int>(i);
        return -1;
    };
    VkPhysicalDevice chosen = VK_NULL_HANDLE;
    for (VkPhysicalDevice d : devices)
    {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(d, &props);
        std::cerr << "[Vulkan] device vendor=" << props.vendorID << " name=" << props.deviceName << "\n";
        if (props.vendorID == kVendorNvidia && graphicsFamily(d) >= 0)
        {
            chosen = d;
            break;
        }
    }
    if (!chosen)
        for (VkPhysicalDevice d : devices)
            if (graphicsFamily(d) >= 0)
            {
                chosen = d;
                break;
            }
    if (!chosen)
        return false;
    gpu_.physical = chosen;
    gpu_.queueFamily = static_cast<uint32>(graphicsFamily(chosen));
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(chosen, &props);
    std::cerr << "[Vulkan] using " << props.deviceName << "\n";
    gpu_.maxAnisotropy = props.limits.maxSamplerAnisotropy;
    return true;
}

bool DeviceContext::createDevice()
{
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{};
    qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    qi.queueFamilyIndex = gpu_.queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;

    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(gpu_.physical, &supported);
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = supported.samplerAnisotropy; // only request what exists
    gpu_.anisotropy = supported.samplerAnisotropy == VK_TRUE;

    const char* swapchainExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount = 1;
    ci.pQueueCreateInfos = &qi;
    ci.enabledExtensionCount = 1;
    ci.ppEnabledExtensionNames = &swapchainExt;
    ci.pEnabledFeatures = &features;
    if (vkCreateDevice(gpu_.physical, &ci, nullptr, &gpu_.device) != VK_SUCCESS)
        return false;
    vkGetDeviceQueue(gpu_.device, gpu_.queueFamily, 0, &gpu_.queue);
    return true;
}

bool DeviceContext::createSwapchain(VkSwapchainKHR old)
{
    uint32 n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_.physical, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_.physical, surface_, &n, formats.data());
    // A UNORM format: the original writes texture x vertex colour straight to the back buffer,
    // so the lit values are already display-referred.
    swapchainFormat_ = formats.empty() ? VK_FORMAT_B8G8R8A8_UNORM : formats[0].format;
    for (const VkSurfaceFormatKHR& f : formats)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM)
        {
            swapchainFormat_ = f.format;
            break;
        }

    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_.physical, surface_, &caps);
    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface_;
    ci.minImageCount = std::max(3u, caps.minImageCount);
    if (caps.maxImageCount > 0)
        ci.minImageCount = std::min(ci.minImageCount, caps.maxImageCount);
    ci.imageFormat = swapchainFormat_;
    ci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    ci.imageExtent = {width_, height_};
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = old;
    if (vkCreateSwapchainKHR(gpu_.device, &ci, nullptr, &swapchain_) != VK_SUCCESS)
        return false;
    vkGetSwapchainImagesKHR(gpu_.device, swapchain_, &n, nullptr);
    std::vector<VkImage> images(n);
    vkGetSwapchainImagesKHR(gpu_.device, swapchain_, &n, images.data());
    for (VkImage img : images)
        swapchainViews_.push_back(createView(gpu_, img, swapchainFormat_, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_VIEW_TYPE_2D, 0u, 1u));
    std::cerr << "[Vulkan] swapchain " << width_ << "x" << height_ << " images=" << n << " format=" << static_cast<int>(swapchainFormat_) << "\n";
    return true;
}

bool DeviceContext::rebuildSwapchain()
{
    VkSurfaceCapabilitiesKHR caps{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_.physical, surface_, &caps);
    // The game draws at a fixed size: wait until the surface takes it again (not minimised, the
    // fullscreen mode restored).
    if (caps.currentExtent.width != 0xFFFFFFFFu && (caps.currentExtent.width != width_ || caps.currentExtent.height != height_))
        return false;
    vkDeviceWaitIdle(gpu_.device);
    for (VkImageView v : swapchainViews_)
        vkDestroyImageView(gpu_.device, v, nullptr);
    swapchainViews_.clear();
    const VkSwapchainKHR old = swapchain_;
    swapchain_ = VK_NULL_HANDLE;
    const bool ok = createSwapchain(old);
    vkDestroySwapchainKHR(gpu_.device, old, nullptr);
    if (!ok)
        return false;
    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (VkSemaphore s : renderFinished_)
        vkDestroySemaphore(gpu_.device, s, nullptr);
    renderFinished_.assign(swapchainViews_.size(), VK_NULL_HANDLE);
    for (VkSemaphore& s : renderFinished_)
        vkCheck(vkCreateSemaphore(gpu_.device, &si, nullptr, &s), "vkCreateSemaphore");
    // The acquire semaphore may have been left pending by a failed acquire: start a fresh one.
    vkDestroySemaphore(gpu_.device, imageAvailable_, nullptr);
    vkCheck(vkCreateSemaphore(gpu_.device, &si, nullptr, &imageAvailable_), "vkCreateSemaphore");
    ++swapchainGeneration_;
    swapchainStale_ = false;
    return true;
}

bool DeviceContext::acquire()
{
    if (swapchainStale_ && !rebuildSwapchain())
        return false;
    VkResult r = vkAcquireNextImageKHR(gpu_.device, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex_);
    if (r == VK_ERROR_OUT_OF_DATE_KHR)
    {
        // Alt-tab out of fullscreen, a display change: a new swapchain, then try again.
        if (!rebuildSwapchain())
            return false;
        r = vkAcquireNextImageKHR(gpu_.device, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex_);
    }
    // SUBOPTIMAL still delivers an image; anything else skips the frame (e.g. while minimised).
    if (r == VK_SUBOPTIMAL_KHR)
        swapchainStale_ = true;
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR)
    {
        std::cerr << "[Vulkan] vkAcquireNextImageKHR: " << static_cast<int>(r) << " (frame skipped)\n";
        return false;
    }
    VkCommandBufferBeginInfo bi{};
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkCheck(vkBeginCommandBuffer(cmd_, &bi), "vkBeginCommandBuffer");
    return true;
}

void DeviceContext::submitAndPresent()
{
    vkCheck(vkEndCommandBuffer(cmd_), "vkEndCommandBuffer");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &imageAvailable_;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd_;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &renderFinished_[imageIndex_];
    vkCheck(vkQueueSubmit(gpu_.queue, 1, &si, fence_), "vkQueueSubmit");
    vkCheck(vkWaitForFences(gpu_.device, 1, &fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vkCheck(vkResetFences(gpu_.device, 1, &fence_), "vkResetFences");

    VkPresentInfoKHR pi{};
    pi.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &renderFinished_[imageIndex_];
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain_;
    pi.pImageIndices = &imageIndex_;
    // Out of date / suboptimal: rebuilt before the next frame (rebuildSwapchain).
    const VkResult r = vkQueuePresentKHR(gpu_.queue, &pi);
    if (r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR)
        swapchainStale_ = true;
    else if (r != VK_SUCCESS)
        vkCheck(r, "vkQueuePresentKHR");
}

void DeviceContext::waitIdle() const
{
    if (gpu_.device)
        vkDeviceWaitIdle(gpu_.device);
}

void DeviceContext::destroy()
{
    if (gpu_.device)
    {
        vkDeviceWaitIdle(gpu_.device);
        for (VkSemaphore s : renderFinished_)
            vkDestroySemaphore(gpu_.device, s, nullptr);
        renderFinished_.clear();
        if (imageAvailable_)
            vkDestroySemaphore(gpu_.device, imageAvailable_, nullptr);
        if (fence_)
            vkDestroyFence(gpu_.device, fence_, nullptr);
        if (framePool_)
            vkDestroyCommandPool(gpu_.device, framePool_, nullptr);
        if (gpu_.uploadPool)
            vkDestroyCommandPool(gpu_.device, gpu_.uploadPool, nullptr);
        for (VkImageView v : swapchainViews_)
            vkDestroyImageView(gpu_.device, v, nullptr);
        swapchainViews_.clear();
        if (swapchain_)
            vkDestroySwapchainKHR(gpu_.device, swapchain_, nullptr);
        vkDestroyDevice(gpu_.device, nullptr);
    }
    if (surface_)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (instance_)
        vkDestroyInstance(instance_, nullptr);
    if (window_)
        SDL_DestroyWindow(window_);
    *this = DeviceContext{};
    SDL_Quit();
}

} // namespace Renderer::Backend
