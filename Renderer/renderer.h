// Renderer/renderer.h - renderer facade: owns the scene and the Vulkan device.
#pragma once
#include "engine_types.h"
#include "Renderer/scene.h"
#include "Renderer/texture.h"
#include "Renderer/ui.h"
#include "Renderer/backend/vulkan.h"

namespace Renderer {

// Drives the per-frame pipeline: beginFrame -> renderScene -> present.
class Renderer {
public:
    Renderer() = default;
    ~Renderer() { shutdown(); }

    Renderer(Renderer const&) = delete;
    Renderer& operator=(Renderer const&) = delete;

    // Create the window + device. Returns false if Vulkan could not be initialised.
    [[nodiscard]] bool initialize(uint32 width, uint32 height, const Backend::VulkanCreateParams& extra = {});
    [[nodiscard]] bool isReady() const noexcept { return device_.isReady(); }
    [[nodiscard]] Backend::Vulkan* device() noexcept { return &device_; }
    void shutdown() noexcept { device_.shutdown(); }

    // --- frame lifecycle ---
    void beginFrame() noexcept { device_.beginFrame(); }
    // Draws `scene`, then this frame's 2D overlay (ui()), which present() then clears.
    void renderScene(const Scene& scene) noexcept { device_.drawScene(scene, ui_); }
    void present() noexcept
    {
        device_.present();
        ui_.clear();
    }

    // 2D overlay for the current frame (menus, HUD). Fill it before renderScene().
    [[nodiscard]] UiBatch& ui() noexcept { return ui_; }

    // GPU pass times and draw counts of the last presented frame.
    [[nodiscard]] const FrameStats& frameStats() const noexcept { return device_.frameStats(); }

    [[nodiscard]] Scene& scene() noexcept { return scene_; }
    [[nodiscard]] const Scene& scene() const noexcept { return scene_; }

    // Upload `tex`'s pixels and show it as a fullscreen overlay until hideVideo().
    void showVideo(const Texture& tex) noexcept { device_.setVideoFrame(tex); }
    void hideVideo() noexcept { device_.clearVideo(); }

private:
    Scene scene_;              // ~Renderer shuts the device down before the scene's meshes are destroyed
    UiBatch ui_;
    Backend::Vulkan device_;
};

} // namespace Renderer
