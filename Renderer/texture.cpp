// Renderer/texture.cpp - CPU-side texture implementation.
#include "Renderer/texture.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace {

uint32 nextHandle() noexcept {
    static std::atomic<uint32> next{1u};
    return next.fetch_add(1u, std::memory_order_relaxed);
}

uint8_t toByte(float v) noexcept {
    // NaN fails both comparisons and maps to 0.
    if (!(v > 0.0f))
        return 0u;
    if (v >= 1.0f)
        return 255u;
    return static_cast<uint8_t>(std::lround(v * 255.0f));
}

} // namespace

namespace Renderer {

bool Texture::create(uint32 width, uint32 height) {
    if (width == 0u || height == 0u)
        return false;
    width_ = width;
    height_ = height;
    handle_ = nextHandle();
    rgba_.clear();
    ++revision_;
    return true;
}

bool Texture::setPixels(std::span<const Math::Vector4> pixels) {
    const std::size_t count = static_cast<std::size_t>(width_) * height_;
    if (!valid() || pixels.size() != count)
        return false;
    rgba_.resize(count * 4u);
    for (std::size_t i = 0; i < count; ++i) {
        const Math::Vector4& p = pixels[i];
        rgba_[i * 4u + 0u] = toByte(p.x);
        rgba_[i * 4u + 1u] = toByte(p.y);
        rgba_[i * 4u + 2u] = toByte(p.z);
        rgba_[i * 4u + 3u] = toByte(p.w);
    }
    ++revision_;
    return true;
}

bool Texture::setPixelsRGBA8(std::span<const uint8_t> rgba) {
    const std::size_t bytes = static_cast<std::size_t>(width_) * height_ * 4u;
    if (!valid() || rgba.size() != bytes)
        return false;
    rgba_.assign(rgba.begin(), rgba.end());
    ++revision_;
    return true;
}

void Texture::release() noexcept {
    handle_ = 0u;
    width_ = 0u;
    height_ = 0u;
    rgba_.clear();
    rgba_.shrink_to_fit();
    ++revision_;
}

} // namespace Renderer
