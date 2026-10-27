// Renderer/renderer.cpp - renderer facade.
#include "Renderer/renderer.h"

namespace Renderer {

bool Renderer::initialize(uint32 width, uint32 height, const Backend::VulkanCreateParams& extra) {
    Backend::VulkanCreateParams params = extra;
    params.width = width;
    params.height = height;
    return device_.create(params);
}

} // namespace Renderer
