// Physics/triangle_field.h - static triangle-soup collision (level world geometry).
#pragma once
#include "engine_types.h"

#include <optional>
#include <vector>

#include "Math/vector.h"

namespace Physics {

// A static set of scene-space triangles with a uniform XZ grid broadphase. Resolves a sphere
// against it: closest point on each nearby triangle, push out along (centre - closest point).
// Triangles are double-sided, so winding does not matter.
class TriangleField {
public:
    struct Triangle { Math::Vector3 a, b, c; };

    // Replaces the field's triangles and rebuilds the grid. Degenerate triangles are dropped.
    void build(std::vector<Triangle> triangles, float32 cellSize = 4.0f);
    // Takes build()'s triangles [first, first + count) out of every query (a prop that loses
    // its collision when damaged).
    void remove(uint32 first, uint32 count);
    // Puts them back (a prop given its collision again).
    void restore(uint32 first, uint32 count);
    // build()'s triangles [first, first + count) that it kept (degenerates dropped).
    [[nodiscard]] std::vector<Triangle> sourceTriangles(uint32 first, uint32 count) const;

    // Pushes a sphere (centre `p`, radius `r`) out of every overlapping triangle. A contact
    // whose push direction has y > 0.5 is ground: it zeroes downward `v.y` and makes the call
    // return true. Other contacts remove the velocity component into the surface.
    bool resolveSphere(Math::Vector3& p, float32 r, Math::Vector3& v) const;

    // First hit of the segment from -> to, as a fraction of its length (0..1), if any.
    [[nodiscard]] std::optional<float32> raycast(const Math::Vector3& from, const Math::Vector3& to) const;
    // The same, with the triangle hit (index in build order) and its normal facing `from`.
    struct RayHit {
        float32 fraction = 0.0f;
        uint32 triangle = 0u;
        Math::Vector3 normal{0.0f, 1.0f, 0.0f};
    };
    [[nodiscard]] std::optional<RayHit> raycastHit(const Math::Vector3& from, const Math::Vector3& to) const;

    // The highest surface at (x, z) at or below `fromY`: its height, the triangle (index in
    // build order) and the barycentric weights of (x, z) on it (corner a = 1 - u - w).
    struct GroundHit {
        float32 height = 0.0f;
        uint32 triangle = 0u;
        float32 u = 0.0f, w = 0.0f;
    };
    [[nodiscard]] std::optional<GroundHit> groundHit(float32 x, float32 z, float32 fromY) const;
    [[nodiscard]] std::optional<float32> groundBelow(float32 x, float32 z, float32 fromY) const {
        const auto hit = groundHit(x, z, fromY);
        return hit ? std::optional<float32>(hit->height) : std::nullopt;
    }

    [[nodiscard]] std::size_t size() const noexcept { return tris_.size(); }

    // Closest point to `p` on triangle (a, b, c) (Ericson, Real-Time Collision Detection 5.1.5).
    [[nodiscard]] static Math::Vector3 closestPoint(const Math::Vector3& p, const Triangle& t) noexcept;

private:
    void query(float32 minX, float32 minZ, float32 maxX, float32 maxZ, std::vector<uint32>& out) const;
    [[nodiscard]] int32 cellX(float32 x) const noexcept;
    [[nodiscard]] int32 cellZ(float32 z) const noexcept;

    std::vector<Triangle> tris_;
    std::vector<uint32> sourceIndex_; // tris_[i] was triangle sourceIndex_[i] of build()'s input
    std::vector<std::vector<uint32>> cells_; // cellsX_ * cellsZ_, row-major in z
    float32 originX_ = 0.0f, originZ_ = 0.0f, cell_ = 4.0f;
    int32 cellsX_ = 0, cellsZ_ = 0;
};

} // namespace Physics
