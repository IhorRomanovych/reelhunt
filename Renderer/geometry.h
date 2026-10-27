// Renderer/geometry.h - indexed triangle geometry (position/normal/uv/colour vertices) + bounds.
#pragma once
#include "engine_types.h"
#include <algorithm>
#include <vector>
#include "Math/vector.h"

namespace Renderer {

struct BoundingBox {
    Math::Vector3 min = Math::Vector3{0.0f, 0.0f, 0.0f};
    Math::Vector3 max = Math::Vector3{0.0f, 0.0f, 0.0f};
    bool hasData = false;

    [[nodiscard]] bool empty() const noexcept { return !hasData; }
    [[nodiscard]] Math::Vector3 center() const noexcept { return (min + max) * 0.5f; }
    [[nodiscard]] Math::Vector3 size() const noexcept { return max - min; }
    void expand(const Math::Vector3& p) noexcept {
        if (!hasData) { min = p; max = p; hasData = true; return; }
        min.x = std::min(min.x, p.x); min.y = std::min(min.y, p.y); min.z = std::min(min.z, p.z);
        max.x = std::max(max.x, p.x); max.y = std::max(max.y, p.y); max.z = std::max(max.z, p.z);
    }
};

struct Vertex {
    Math::Vector3 position = Math::Vector3{};
    Math::Vector3 normal = Math::Vector3{0.0f, 0.0f, 1.0f};
    Math::Vector2 uv = Math::Vector2{0.0f, 0.0f};
    Math::Vector4 color = Math::Vector4{1.0f, 1.0f, 1.0f, 1.0f}; // baked light for MeshLighting::Baked
};

struct Triangle {
    uint32 a = 0u;
    uint32 b = 0u;
    uint32 c = 0u;
};

class Geometry {
public:
    void addVertex(const Math::Vector3& position, const Math::Vector3& normal, const Math::Vector2& uv,
                   const Math::Vector4& color = Math::Vector4{1.0f, 1.0f, 1.0f, 1.0f}) {
        vertices_.push_back({position, normal, uv, color});
    }
    // Triangles referencing a vertex that does not exist (yet) are rejected; returns whether added.
    bool addTriangle(uint32 a, uint32 b, uint32 c) {
        const auto n = vertices_.size();
        if (a >= n || b >= n || c >= n)
            return false;
        triangles_.push_back({a, b, c});
        return true;
    }

    [[nodiscard]] uint32 vertexCount() const noexcept { return static_cast<uint32>(vertices_.size()); }
    [[nodiscard]] uint32 triangleCount() const noexcept { return static_cast<uint32>(triangles_.size()); }
    [[nodiscard]] const std::vector<Vertex>& vertices() const noexcept { return vertices_; }
    [[nodiscard]] const std::vector<Triangle>& triangles() const noexcept { return triangles_; }
    [[nodiscard]] const Vertex& vertex(uint32 index) const { return vertices_.at(index); }
    [[nodiscard]] const Triangle& triangle(uint32 index) const { return triangles_.at(index); }

    void computeBoundingBox() noexcept {
        bb_ = BoundingBox{};
        for (const auto& v : vertices_)
            bb_.expand(v.position);
    }
    [[nodiscard]] const BoundingBox& boundingBox() const noexcept { return bb_; }

private:
    std::vector<Vertex> vertices_;
    std::vector<Triangle> triangles_;
    BoundingBox bb_;
};

} // namespace Renderer
