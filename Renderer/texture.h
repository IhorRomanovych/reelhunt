// Renderer/texture.h - CPU-side texture: a unique handle, size and level-0 RGBA8 pixels.
#pragma once
#include "engine_types.h"
#include <cstdint>
#include <span>
#include <vector>
#include "Math/vector.h"

namespace Renderer {

// The GPU backend caches its image by handle(), so a texture's handle must stay stable while
// the texture is in use; copying a Texture copies its handle (both copies alias one GPU image).
// Only level 0 is stored; the backend generates the mip chain on upload.
class Texture {
public:
    // Allocate a width x height texture with a fresh handle. Fails on a zero dimension.
    [[nodiscard]] bool create(uint32 width, uint32 height);

    // Replace the pixels from linear 0..1 floats (clamped). `pixels` must hold width*height
    // entries; returns false (and leaves the texture unchanged) otherwise.
    bool setPixels(std::span<const Math::Vector4> pixels);
    // Replace the pixels from tightly packed RGBA8 bytes (width*height*4 bytes).
    bool setPixelsRGBA8(std::span<const uint8_t> rgba);

    [[nodiscard]] uint32 handle() const noexcept { return handle_; }
    [[nodiscard]] uint32 width() const noexcept { return width_; }
    [[nodiscard]] uint32 height() const noexcept { return height_; }
    [[nodiscard]] bool valid() const noexcept { return handle_ != 0u; }
    [[nodiscard]] bool hasPixels() const noexcept { return !rgba_.empty(); }
    [[nodiscard]] const std::vector<uint8_t>& rgba8() const noexcept { return rgba_; }

    // Bumped on every setPixels*; lets the backend notice in-place updates (video frames).
    [[nodiscard]] uint32 revision() const noexcept { return revision_; }

    void release() noexcept;

private:
    uint32 handle_ = 0u;
    uint32 width_ = 0u;
    uint32 height_ = 0u;
    uint32 revision_ = 0u;
    std::vector<uint8_t> rgba_;
};

} // namespace Renderer
