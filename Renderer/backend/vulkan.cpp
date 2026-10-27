// Renderer/backend/vulkan.cpp - the Vulkan renderer's frame orchestration. See vulkan.h.
#include "Renderer/backend/vulkan.h"

#include "Renderer/backend/vk_pipeline.h"
#include "Renderer/camera.h"
#include "Renderer/shadow_cascades.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace Renderer::Backend {

bool Vulkan::create(const VulkanCreateParams& p) noexcept
{
    // REELHUNT_DEBUG_VIEW shows one G-buffer input instead of the lit scene (renderer debugging).
    if (const char* v = std::getenv("REELHUNT_DEBUG_VIEW"))
    {
        const std::string view = v;
        debugView_ = view == "albedo" ? 1.0f : view == "light" ? 2.0f : view == "normal" ? 3.0f : view == "baked" ? 4.0f : view == "product" ? 5.0f : 0.0f;
    }
    if (ready_ || !device_.create(p))
        return false;
    const GpuContext& gpu = device_.gpu();
    cache_.create(gpu);
    frame_.create(gpu);
    sceneLayout_ = createPipelineLayout(gpu.device, {frame_.layout(), cache_.materialLayout()}, sizeof(DrawConstants));
    shadows_.create(gpu, sceneLayout_);
    lampShadows_.create(gpu, sceneLayout_);
    gbuffer_.create(gpu, device_.width(), device_.height(), sceneLayout_);
    lighting_.create(gpu, device_.width(), device_.height(), frame_.layout(), gbuffer_, shadows_, lampShadows_);
    forward_.create(gpu, device_.width(), device_.height(), frame_.layout(), cache_.materialLayout(), gbuffer_, lighting_,
                    shadows_, lampShadows_);
    composite_.create(device_, cache_, lighting_.output());
    timer_.create(gpu);
    ready_ = true;
    return true;
}

void Vulkan::shutdown() noexcept
{
    if (ready_)
    {
        device_.waitIdle();
        timer_.destroy();
        composite_.destroy();
        forward_.destroy();
        lighting_.destroy();
        gbuffer_.destroy();
        lampShadows_.destroy();
        shadows_.destroy();
        vkDestroyPipelineLayout(device_.gpu().device, sceneLayout_, nullptr);
        frame_.destroy();
        cache_.destroy();
        ready_ = false;
    }
    if (device_.window())
        device_.destroy();
}

void Vulkan::beginFrame() noexcept
{
    frameActive_ = ready_ && device_.acquire();
    if (frameActive_)
        composite_.syncSwapchain(device_);
}

FrameUniforms Vulkan::frameUniforms(const Scene& scene, uint32& cascadeCount) const
{
    FrameUniforms f;
    const SceneLighting& lit = scene.lighting();
    const Camera* cam = scene.activeCamera();
    if (cam)
    {
        f.view = cam->view().toGlm();
        f.viewProj = cam->clip().toGlm();
        const Math::Vector3 p = cam->position();
        f.cameraPos = glm::vec4(p.x, p.y, p.z, 1.0f);
    }
    f.invViewProj = glm::inverse(f.viewProj);
    f.viewport = glm::vec4(static_cast<float32>(device_.width()), static_cast<float32>(device_.height()),
                           static_cast<float32>(draws_.lights.size()), static_cast<float32>(lit.mode));
    const glm::vec3 sunDir = glm::normalize(glm::vec3(lit.sunDirection.x, lit.sunDirection.y, lit.sunDirection.z));
    const bool sky = lit.mode == LightingMode::Midday || lit.mode == LightingMode::Dynamic; // sun or moon
    f.sunDirection = glm::vec4(sunDir, sky ? lit.sunIntensity : 0.0f);
    f.ambient = glm::vec4(lit.ambient.x, lit.ambient.y, lit.ambient.z, lit.bakedScale);
    f.lampParams = glm::vec4(1.0f / static_cast<float32>(kLampShadowSize), lit.lampScale, lit.bakedShadowFloor, debugView_);
    f.sky = glm::vec4(lit.skyColor.x, lit.skyColor.y, lit.skyColor.z, 1.0f);
    f.fog = glm::vec4(lit.fogStart, lit.fogEnd, lit.fog && lit.mode != LightingMode::Midday ? 1.0f : 0.0f, 0.0f);

    // Cascaded shadows exist only with a sun or moon.
    cascadeCount = 0u;
    if (cam && sky && lit.shadows)
    {
        CascadeCamera cc;
        cc.position = glm::vec3(f.cameraPos);
        cc.view = f.view;
        cc.fovYDegrees = cam->fov();
        cc.aspect = cam->aspect();
        const CascadeFit fit = fitShadowCascades(cc, cam->nearPlane(), ShadowPass::kSplits.data(), ShadowPass::kCascades,
                                                 sunDir, ShadowPass::kMapSize);
        cascadeCount = fit.count;
        for (uint32 i = 0; i < fit.count; ++i)
        {
            f.cascadeViewProj[i] = fit.cascades[i].viewProj;
            f.cascadeSplits[static_cast<glm::length_t>(i)] = fit.cascades[i].splitFar;
            f.cascadeTexel[static_cast<glm::length_t>(i)] = fit.cascades[i].texelWorld;
        }
    }
    f.sunColor = glm::vec4(lit.sunColor.x, lit.sunColor.y, lit.sunColor.z, cascadeCount > 0 ? 1.0f : 0.0f);
    f.shadowParams = glm::vec4(1.0f / static_cast<float32>(ShadowPass::kMapSize), static_cast<float32>(cascadeCount), 0.0f, 0.0f);
    return f;
}

void Vulkan::drawScene(const Scene& scene, const UiBatch& ui) noexcept
{
    if (!frameActive_)
        return;
    draws_.build(scene, cache_);
    uint32 cascadeCount = 0u;
    const FrameUniforms f = frameUniforms(scene, cascadeCount);
    frame_.upload(f, draws_.bones, draws_.lights);

    VkCommandBuffer cb = device_.cmd();
    timer_.begin(cb);
    shadows_.record(cb, draws_, f, frame_.set(), cascadeCount);
    timer_.mark(cb, GpuPass::SunShadows);
    lampShadows_.record(cb, draws_, frame_.set());
    timer_.mark(cb, GpuPass::LampShadows);
    gbuffer_.record(cb, draws_, f.viewProj, frame_.set());
    timer_.mark(cb, GpuPass::GBuffer);
    lighting_.record(cb, frame_.set());
    timer_.mark(cb, GpuPass::Lighting);
    forward_.record(cb, draws_, f.viewProj, glm::vec3(f.cameraPos), frame_.set());
    timer_.mark(cb, GpuPass::Forward);
    composite_.record(cb, device_.imageIndex(), scene.effects(), ui, cache_);
    timer_.mark(cb, GpuPass::Composite);
    stats_.draws = static_cast<uint32>(draws_.items.size());
    stats_.lights = static_cast<uint32>(draws_.lights.size());
    stats_.shadowLights = draws_.shadowLights;
}

void Vulkan::present() noexcept
{
    if (!frameActive_)
        return;
    frameActive_ = false;
    device_.submitAndPresent(); // waits for the frame's fence, so its timestamps are ready
    timer_.collect();
    stats_.gpu = timer_.last();
}

void Vulkan::setVideoFrame(const Texture& tex) noexcept
{
    if (!ready_ || !tex.valid() || tex.width() == 0u || tex.height() == 0u)
        return;
    composite_.setVideo(cache_.updateTexture(tex));
    videoHandle_ = tex.handle();
}

void Vulkan::clearVideo() noexcept
{
    if (ready_)
        composite_.setVideo(VK_NULL_HANDLE);
    videoHandle_ = 0u;
}

void Vulkan::removeMesh(const Mesh* mesh) noexcept
{
    if (ready_)
        cache_.removeMesh(mesh);
}

void Vulkan::removeTexture(uint32 handle) noexcept
{
    if (!ready_)
        return;
    device_.waitIdle();
    if (handle == videoHandle_)
        clearVideo();
    cache_.removeTexture(handle);
}

void Vulkan::releaseScene(const Scene& scene) noexcept
{
    if (!ready_)
        return;
    device_.waitIdle();
    for (const auto& e : scene.entities())
        if (const auto* mesh = dynamic_cast<const Mesh*>(e.get()))
            cache_.removeMesh(mesh);
}

} // namespace Renderer::Backend
