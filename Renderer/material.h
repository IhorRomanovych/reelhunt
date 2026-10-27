// Renderer/material.h - material: RenderMode flags, colours and a texture reference.
#pragma once
#include "engine_types.h"
#include "Math/vector.h"
#include "Renderer/texture.h"

namespace Renderer {

// Mirrors RW's RpMaterial render-mode + colour + texture model. The texture is a non-owning
// reference: whoever loaded it (LevelContent, Character, ...) keeps it alive for the mesh's life.
class Material {
public:
    // --- render mode ---
    void setRenderMode(Engine::RenderMode mode) noexcept { renderMode_ = mode; }
    [[nodiscard]] Engine::RenderMode renderMode() const noexcept { return renderMode_; }
    [[nodiscard]] bool isFlag(Engine::RenderMode flag) const noexcept {
        return Engine::hasFlag(renderMode_, static_cast<uint32>(flag));
    }
    void setFlag(Engine::RenderMode flag, bool v) noexcept {
        const uint32 bits = static_cast<uint32>(flag);
        const uint32 cur = static_cast<uint32>(renderMode_);
        renderMode_ = static_cast<Engine::RenderMode>(v ? (cur | bits) : (cur & ~bits));
    }

    // --- colours ---
    void setDiffuse(Math::Vector4 c) noexcept { diffuse_ = c; }
    [[nodiscard]] Math::Vector4 diffuse() const noexcept { return diffuse_; }
    void setAmbient(Math::Vector4 c) noexcept { ambient_ = c; }
    [[nodiscard]] Math::Vector4 ambient() const noexcept { return ambient_; }
    void setSpecular(Math::Vector4 c) noexcept { specular_ = c; }
    [[nodiscard]] Math::Vector4 specular() const noexcept { return specular_; }

    // --- texture (non-owning reference) ---
    void setTexture(const Texture& t) noexcept { texture_ = &t; }
    [[nodiscard]] const Texture* texture() const noexcept { return texture_; }
    [[nodiscard]] bool hasTexture() const noexcept { return texture_ != nullptr; }

    // The standard opaque diffuse material used by all static geometry.
    [[nodiscard]] static Material defaultMaterial() noexcept {
        Material m;
        for (Engine::RenderMode f : {Engine::RenderMode::Diffuse, Engine::RenderMode::Gouraud,
                                     Engine::RenderMode::BackCull, Engine::RenderMode::ZWrite,
                                     Engine::RenderMode::ZTestOn, Engine::RenderMode::Visible})
            m.setFlag(f, true);
        return m;
    }

private:
    Engine::RenderMode renderMode_ = Engine::RenderMode::None;
    Math::Vector4 diffuse_{1.0f, 1.0f, 1.0f, 1.0f};
    Math::Vector4 ambient_{0.5f, 0.5f, 0.5f, 1.0f};
    Math::Vector4 specular_{0.5f, 0.5f, 0.5f, 1.0f};
    const Texture* texture_ = nullptr;
};

} // namespace Renderer
