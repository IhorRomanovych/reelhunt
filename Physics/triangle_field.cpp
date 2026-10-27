// Physics/triangle_field.cpp - static triangle-soup collision with a uniform XZ grid.
#include "Physics/triangle_field.h"

#include <algorithm>
#include <cmath>

namespace Physics {

namespace {

// Keeps a grid over a huge (or corrupt) bounding box from allocating millions of cells.
constexpr int32 kMaxCellsPerAxis = 1024;

} // namespace

void TriangleField::build(std::vector<Triangle> triangles, float32 cellSize)
{
    tris_.clear();
    sourceIndex_.clear();
    tris_.reserve(triangles.size());
    for (uint32 i = 0; i < triangles.size(); ++i)
    {
        const Triangle& t = triangles[i];
        if ((t.b - t.a).cross(t.c - t.a).lengthSquared() > 1e-12f)
        {
            tris_.push_back(t);
            sourceIndex_.push_back(i);
        }
    }

    cells_.clear();
    cellsX_ = cellsZ_ = 0;
    if (tris_.empty())
        return;

    float32 minX = 1e30f, minZ = 1e30f, maxX = -1e30f, maxZ = -1e30f;
    for (const auto& t : tris_)
        for (const auto* v : {&t.a, &t.b, &t.c})
        {
            minX = std::min(minX, v->x); maxX = std::max(maxX, v->x);
            minZ = std::min(minZ, v->z); maxZ = std::max(maxZ, v->z);
        }
    const float32 span = std::max(maxX - minX, maxZ - minZ);
    cell_ = std::max(cellSize, span / static_cast<float32>(kMaxCellsPerAxis));
    originX_ = minX;
    originZ_ = minZ;
    cellsX_ = static_cast<int32>((maxX - minX) / cell_) + 1;
    cellsZ_ = static_cast<int32>((maxZ - minZ) / cell_) + 1;
    cells_.resize(static_cast<std::size_t>(cellsX_) * static_cast<std::size_t>(cellsZ_));

    for (uint32 i = 0; i < tris_.size(); ++i)
    {
        const Triangle& t = tris_[i];
        const int32 x0 = cellX(std::min({t.a.x, t.b.x, t.c.x})), x1 = cellX(std::max({t.a.x, t.b.x, t.c.x}));
        const int32 z0 = cellZ(std::min({t.a.z, t.b.z, t.c.z})), z1 = cellZ(std::max({t.a.z, t.b.z, t.c.z}));
        for (int32 z = z0; z <= z1; ++z)
            for (int32 x = x0; x <= x1; ++x)
                cells_[static_cast<std::size_t>(z) * static_cast<std::size_t>(cellsX_) + static_cast<std::size_t>(x)].push_back(i);
    }
}

std::vector<TriangleField::Triangle> TriangleField::sourceTriangles(uint32 first, uint32 count) const
{
    std::vector<Triangle> out;
    for (std::size_t i = 0; i < tris_.size(); ++i)
        if (sourceIndex_[i] >= first && sourceIndex_[i] < first + count)
            out.push_back(tris_[i]);
    return out;
}

void TriangleField::remove(uint32 first, uint32 count)
{
    for (uint32 i = 0; i < tris_.size(); ++i)
    {
        if (sourceIndex_[i] < first || sourceIndex_[i] >= first + count)
            continue;
        const Triangle& t = tris_[i];
        const int32 x0 = cellX(std::min({t.a.x, t.b.x, t.c.x})), x1 = cellX(std::max({t.a.x, t.b.x, t.c.x}));
        const int32 z0 = cellZ(std::min({t.a.z, t.b.z, t.c.z})), z1 = cellZ(std::max({t.a.z, t.b.z, t.c.z}));
        for (int32 z = z0; z <= z1; ++z)
            for (int32 x = x0; x <= x1; ++x)
                std::erase(cells_[static_cast<std::size_t>(z) * static_cast<std::size_t>(cellsX_) + static_cast<std::size_t>(x)], i);
    }
}

void TriangleField::restore(uint32 first, uint32 count)
{
    for (uint32 i = 0; i < tris_.size(); ++i)
    {
        if (sourceIndex_[i] < first || sourceIndex_[i] >= first + count)
            continue;
        const Triangle& t = tris_[i];
        const int32 x0 = cellX(std::min({t.a.x, t.b.x, t.c.x})), x1 = cellX(std::max({t.a.x, t.b.x, t.c.x}));
        const int32 z0 = cellZ(std::min({t.a.z, t.b.z, t.c.z})), z1 = cellZ(std::max({t.a.z, t.b.z, t.c.z}));
        for (int32 z = z0; z <= z1; ++z)
            for (int32 x = x0; x <= x1; ++x)
            {
                std::vector<uint32>& cell = cells_[static_cast<std::size_t>(z) * static_cast<std::size_t>(cellsX_) + static_cast<std::size_t>(x)];
                if (std::find(cell.begin(), cell.end(), i) == cell.end())
                    cell.push_back(i);
            }
    }
}

int32 TriangleField::cellX(float32 x) const noexcept
{
    return std::clamp(static_cast<int32>(std::floor((x - originX_) / cell_)), 0, cellsX_ - 1);
}

int32 TriangleField::cellZ(float32 z) const noexcept
{
    return std::clamp(static_cast<int32>(std::floor((z - originZ_) / cell_)), 0, cellsZ_ - 1);
}

void TriangleField::query(float32 minX, float32 minZ, float32 maxX, float32 maxZ, std::vector<uint32>& out) const
{
    out.clear();
    if (cells_.empty())
        return;
    const int32 x0 = cellX(minX), x1 = cellX(maxX), z0 = cellZ(minZ), z1 = cellZ(maxZ);
    for (int32 z = z0; z <= z1; ++z)
        for (int32 x = x0; x <= x1; ++x)
        {
            const auto& c = cells_[static_cast<std::size_t>(z) * static_cast<std::size_t>(cellsX_) + static_cast<std::size_t>(x)];
            out.insert(out.end(), c.begin(), c.end());
        }
    // A triangle spanning several cells is listed in each of them.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

Math::Vector3 TriangleField::closestPoint(const Math::Vector3& p, const Triangle& t) noexcept
{
    const Math::Vector3 ab = t.b - t.a, ac = t.c - t.a, ap = p - t.a;
    const float32 d1 = ab.dot(ap), d2 = ac.dot(ap);
    if (d1 <= 0.0f && d2 <= 0.0f)
        return t.a;
    const Math::Vector3 bp = p - t.b;
    const float32 d3 = ab.dot(bp), d4 = ac.dot(bp);
    if (d3 >= 0.0f && d4 <= d3)
        return t.b;
    const float32 vc = d1 * d4 - d3 * d2;
    if (vc <= 0.0f && d1 >= 0.0f && d3 <= 0.0f)
        return t.a + ab * (d1 / (d1 - d3));
    const Math::Vector3 cp = p - t.c;
    const float32 d5 = ab.dot(cp), d6 = ac.dot(cp);
    if (d6 >= 0.0f && d5 <= d6)
        return t.c;
    const float32 vb = d5 * d2 - d1 * d6;
    if (vb <= 0.0f && d2 >= 0.0f && d6 <= 0.0f)
        return t.a + ac * (d2 / (d2 - d6));
    const float32 va = d3 * d6 - d5 * d4;
    if (va <= 0.0f && (d4 - d3) >= 0.0f && (d5 - d6) >= 0.0f)
        return t.b + (t.c - t.b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));
    const float32 denom = 1.0f / (va + vb + vc);
    return t.a + ab * (vb * denom) + ac * (vc * denom);
}

std::optional<float32> TriangleField::raycast(const Math::Vector3& from, const Math::Vector3& to) const
{
    const auto hit = raycastHit(from, to);
    return hit ? std::optional<float32>(hit->fraction) : std::nullopt;
}

std::optional<TriangleField::RayHit> TriangleField::raycastHit(const Math::Vector3& from, const Math::Vector3& to) const
{
    std::vector<uint32> candidates;
    query(std::min(from.x, to.x), std::min(from.z, to.z), std::max(from.x, to.x), std::max(from.z, to.z), candidates);
    const Math::Vector3 dir = to - from;
    std::optional<RayHit> best;
    for (uint32 i : candidates)
    {
        // Moller-Trumbore, double-sided.
        const Triangle& t = tris_[i];
        const Math::Vector3 e1 = t.b - t.a, e2 = t.c - t.a;
        const Math::Vector3 pv = dir.cross(e2);
        const float32 det = e1.dot(pv);
        if (std::fabs(det) < 1e-9f)
            continue;
        const float32 inv = 1.0f / det;
        const Math::Vector3 tv = from - t.a;
        const float32 u = tv.dot(pv) * inv;
        if (u < 0.0f || u > 1.0f)
            continue;
        const Math::Vector3 qv = tv.cross(e1);
        const float32 v = dir.dot(qv) * inv;
        if (v < 0.0f || u + v > 1.0f)
            continue;
        const float32 f = e2.dot(qv) * inv;
        if (f >= 0.0f && f <= 1.0f && (!best || f < best->fraction))
        {
            Math::Vector3 n = e1.cross(e2).normalized();
            if (n.dot(dir) > 0.0f)
                n = n * -1.0f;
            best = RayHit{f, sourceIndex_[i], n};
        }
    }
    return best;
}

bool TriangleField::resolveSphere(Math::Vector3& p, float32 r, Math::Vector3& v) const
{
    std::vector<uint32> candidates;
    query(p.x - r, p.z - r, p.x + r, p.z + r, candidates);

    bool grounded = false;
    // A couple of passes settle corners where pushing out of one face pushes into another.
    for (int pass = 0; pass < 2; ++pass)
    {
        bool moved = false;
        for (const uint32 i : candidates)
        {
            const Triangle& t = tris_[i];
            const Math::Vector3 d = p - closestPoint(p, t);
            const float32 dist2 = d.lengthSquared();
            if (dist2 >= r * r)
                continue;
            const float32 dist = std::sqrt(dist2);
            Math::Vector3 n;
            if (dist > 1e-6f)
            {
                n = d * (1.0f / dist);
            }
            else
            {
                // Centre exactly on the surface: push along the face normal, upward-facing side.
                n = (t.b - t.a).cross(t.c - t.a).normalized();
                if (n.y < 0.0f)
                    n = n * -1.0f;
            }
            p += n * (r - dist);
            moved = true;
            if (n.y > 0.5f)
            {
                v.y = std::max(v.y, 0.0f);
                grounded = true;
            }
            else if (const float32 vn = v.dot(n); vn < 0.0f)
            {
                v -= n * vn;
            }
        }
        if (!moved)
            break;
    }
    return grounded;
}

std::optional<TriangleField::GroundHit> TriangleField::groundHit(float32 x, float32 z, float32 fromY) const
{
    std::vector<uint32> candidates;
    query(x, z, x, z, candidates);
    std::optional<GroundHit> best;
    for (const uint32 i : candidates)
    {
        const Triangle& t = tris_[i];
        // Barycentric coordinates of (x, z) in the triangle's XZ projection.
        const float32 e1x = t.b.x - t.a.x, e1z = t.b.z - t.a.z;
        const float32 e2x = t.c.x - t.a.x, e2z = t.c.z - t.a.z;
        const float32 det = e1x * e2z - e2x * e1z;
        if (std::fabs(det) < 1e-9f)
            continue; // vertical wall
        const float32 px = x - t.a.x, pz = z - t.a.z;
        const float32 u = (px * e2z - e2x * pz) / det;
        const float32 w = (e1x * pz - px * e1z) / det;
        if (u < 0.0f || w < 0.0f || u + w > 1.0f)
            continue;
        const float32 y = t.a.y + u * (t.b.y - t.a.y) + w * (t.c.y - t.a.y);
        if (y <= fromY && (!best || y > best->height))
            best = GroundHit{y, sourceIndex_[i], u, w};
    }
    return best;
}

} // namespace Physics
