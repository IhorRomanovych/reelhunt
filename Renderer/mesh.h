// Renderer/mesh.h - renderable mesh: geometry, material, bounds, optional skin data.
#pragma once
#include "engine_types.h"
#include "renderware_api_stub.h"
#include "Renderer/geometry.h"
#include "Math/matrix.h"
#include "Renderer/material.h"
#include "Renderer/skin_pose.h"
#include "Renderer/smartref.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Renderer {

// How a mesh's surface is lit.
enum class MeshLighting : uint8 {
    Dynamic, // by the scene's ambient, sun and point lights (characters, unlit props)
    Baked,   // by its vertex colours: the level's prelight (world, prelit props)
    Unlit,   // its texture times its material colour and alpha, no light and no fog (translucent
             // effects: a pickup's halo)
};

// A renderable mesh in the scene. Rigid vertices are in model space and placed by transform()
// (identity for geometry already placed in world space); skinned vertices are placed by the
// bone matrices of their SkinPose.
class Mesh : public Engine::Rw::Entity {
public:
    Mesh() : Engine::Rw::Entity(static_cast<uint32>(ObjectType::Mesh)) {}

    // --- geometry ---
    void addVertex(const Math::Vector3& position, const Math::Vector3& normal, const Math::Vector2& uv,
                   const Math::Vector4& color = Math::Vector4{1.0f, 1.0f, 1.0f, 1.0f}) {
        geometry_.addVertex(position, normal, uv, color);
    }
    bool addTriangle(uint32 a, uint32 b, uint32 c) { return geometry_.addTriangle(a, b, c); }
    [[nodiscard]] uint32 vertexCount() const noexcept { return geometry_.vertexCount(); }
    [[nodiscard]] uint32 triangleCount() const noexcept { return geometry_.triangleCount(); }
    [[nodiscard]] const Geometry& geometry() const noexcept { return geometry_; }

    // --- material / texture ---
    void setMaterial(const Material& m) noexcept { material_ = m; }
    [[nodiscard]] const Material& material() const noexcept { return material_; }
    void setTexture(const Texture& t) noexcept { material_.setTexture(t); }

    // --- placement (rigid meshes) ---
    void setTransform(const Math::Matrix4x4& modelToWorld) noexcept { transform_ = modelToWorld; }
    [[nodiscard]] const Math::Matrix4x4& transform() const noexcept { return transform_; }

    // --- lighting ---
    void setLighting(MeshLighting l) noexcept { lighting_ = l; }
    [[nodiscard]] MeshLighting lighting() const noexcept { return lighting_; }
    // Ambient light for this mesh when dynamically lit (e.g. sampled from the ground under a
    // character); w = 0 uses the scene's ambient.
    void setAmbientLight(const Math::Vector4& rgbw) noexcept { ambient_ = rgbw; }
    [[nodiscard]] const Math::Vector4& ambientLight() const noexcept { return ambient_; }
    void setCastsShadow(bool v) noexcept { castsShadow_ = v; }
    [[nodiscard]] bool castsShadow() const noexcept { return castsShadow_; }
    // A moving caster (characters, movable props): redrawn into the lamps' shadow maps every
    // frame; static casters are drawn once. Skinned meshes always count as moving.
    void setMovingCaster(bool v) noexcept { movingCaster_ = v; }
    [[nodiscard]] bool movingCaster() const noexcept { return movingCaster_ || skinned_; }
    // A shadow painted into the level (the blob under a van or a skip): drawn only where the
    // scene has no real shadows of its own to replace it (not under a sun or moon).
    void setPaintedShadow(bool v) noexcept { paintedShadow_ = v; }
    [[nodiscard]] bool paintedShadow() const noexcept { return paintedShadow_; }

    // --- bounding box ---
    [[nodiscard]] const BoundingBox& boundingBox() const noexcept { return geometry_.boundingBox(); }
    void computeBoundingBox() noexcept { geometry_.computeBoundingBox(); }

    // --- skinning (optional; empty when the mesh is rigid) ---
    // Per-vertex, parallel to the geometry vertices. When set, the backend uploads the skinned
    // vertex stream and draws through the skinned pipelines with skinPose()'s matrices.
    void setSkinData(std::vector<std::array<uint8_t, 4>> indices, std::vector<std::array<float, 4>> weights) {
        skinIndices_ = std::move(indices);
        skinWeights_ = std::move(weights);
        skinned_ = !skinIndices_.empty() && skinIndices_.size() == geometry_.vertexCount() &&
                   skinWeights_.size() == skinIndices_.size();
    }
    [[nodiscard]] bool isSkinned() const noexcept { return skinned_; }
    [[nodiscard]] const std::vector<std::array<uint8_t, 4>>& skinIndices() const noexcept { return skinIndices_; }
    [[nodiscard]] const std::vector<std::array<float, 4>>& skinWeights() const noexcept { return skinWeights_; }
    void setSkinPose(std::shared_ptr<const SkinPose> pose) noexcept { skinPose_ = std::move(pose); }
    [[nodiscard]] const SkinPose* skinPose() const noexcept { return skinPose_.get(); }

    // --- visibility (debug markers can be hidden) ---
    void setVisible(bool v) noexcept { visible_ = v; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

private:
    Geometry geometry_;
    Material material_ = Material::defaultMaterial();
    Math::Matrix4x4 transform_ = Math::Matrix4x4::identity();
    MeshLighting lighting_ = MeshLighting::Dynamic;
    Math::Vector4 ambient_{0.0f, 0.0f, 0.0f, 0.0f};
    bool castsShadow_ = true;
    bool movingCaster_ = false;
    bool paintedShadow_ = false;
    bool skinned_ = false;
    bool visible_ = true;
    std::shared_ptr<const SkinPose> skinPose_;
    std::vector<std::array<uint8_t, 4>> skinIndices_;
    std::vector<std::array<float, 4>> skinWeights_;
};

} // namespace Renderer
