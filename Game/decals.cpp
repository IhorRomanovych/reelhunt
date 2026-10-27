// Game/decals.cpp - marks projected onto the world. See decals.h.
#include "Game/decals.h"

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "Game/hud.h"
#include "Game/level_content.h"
#include "Renderer/backend/vulkan.h"
#include "Renderer/mesh.h"
#include "Renderer/scene.h"

namespace Game {

namespace {

struct DecalType {
    const char* texture;
    float32 minSize, maxSize;
    uint8 alpha;
};
// 0x484920 (the callers of types 0..7 give their own sizes).
constexpr std::array<DecalType, Decals::kTypes> kDecalTypes{{
    {"test", 0.0f, 0.0f, 255},       {"blood", 0.0f, 0.0f, 255},      {"blood2", 0.0f, 0.0f, 255},
    {"blood3", 0.0f, 0.0f, 255},     {"blood_dead", 0.0f, 0.0f, 255}, {"melee", 0.0f, 0.0f, 255},
    {"explos", 0.0f, 0.0f, 255},     {"fluid", 0.0f, 0.0f, 255},      {"test", 0.06f, 0.1f, 200},
    {"bh_metal", 0.06f, 0.1f, 200},  {"bh_stone", 0.03f, 0.07f, 200}, {"bh_wood", 0.06f, 0.1f, 200},
    {"bh_metal", 0.06f, 0.1f, 200},  {"bh_stone", 0.06f, 0.1f, 200},  {"bh_metal", 0.06f, 0.1f, 200},
    {"bh_glass", 0.2f, 0.26f, 128},  {"bh_stone", 0.03f, 0.07f, 200}, {"bh_metal", 0.06f, 0.1f, 200},
    {"bh_wood", 0.06f, 0.1f, 200},   {"bh_fabric", 0.06f, 0.1f, 200}, {"bh_stone", 0.06f, 0.1f, 200},
    {"bh_fabric", 0.06f, 0.1f, 200}, {"bh_fabric", 0.06f, 0.1f, 200}, {"bh_fabric", 0.06f, 0.1f, 200},
    {"bh_fabric", 0.06f, 0.1f, 200}, {"bh_fabric", 0.06f, 0.1f, 200},
}};

constexpr float32 kLift = 0.01f;   // off the surface (ours; the forward pass adds its decal bias)
constexpr float32 kFacing = 0.25f; // a triangle at least this square-on to the decal takes it (ours)

// 0x614d50 & 0x7ff over 2047: the exe's uniform draw.
float32 draw(float32 lo, float32 hi, std::mt19937& rng)
{
    return lo + (hi - lo) * static_cast<float32>(std::uniform_int_distribution<int32>(0, 0x7ff)(rng)) / 2047.0f;
}

struct ClipVertex {
    Math::Vector3 p;
    Math::Vector4 light;
};

// Sutherland-Hodgman: keeps the side where dot(p - at, axis) <= limit.
std::vector<ClipVertex> clip(const std::vector<ClipVertex>& in, const Math::Vector3& at, const Math::Vector3& axis, float32 limit)
{
    std::vector<ClipVertex> out;
    for (std::size_t i = 0; i < in.size(); ++i)
    {
        const ClipVertex& a = in[i];
        const ClipVertex& b = in[(i + 1) % in.size()];
        const float32 da = (a.p - at).dot(axis) - limit, db = (b.p - at).dot(axis) - limit;
        if (da <= 0.0f)
            out.push_back(a);
        if ((da < 0.0f) != (db < 0.0f) && da != db)
        {
            const float32 t = da / (da - db);
            const Math::Vector4 l{a.light.x + (b.light.x - a.light.x) * t, a.light.y + (b.light.y - a.light.y) * t,
                                  a.light.z + (b.light.z - a.light.z) * t, 1.0f};
            out.push_back({a.p + (b.p - a.p) * t, l});
        }
    }
    return out;
}

} // namespace

void Decals::attach(Renderer::Scene& scene, Renderer::Backend::Vulkan* device, const LevelContent& level, const Hud& textures)
{
    scene_ = &scene;
    device_ = device;
    level_ = &level;
    textures_ = &textures;
    ring_.clear();
}

uint32 Decals::add(int32 type, const Math::Vector3& at, const Math::Vector3& normal, float32 size, float32 rotation,
                   std::mt19937& rng)
{
    if (!scene_ || !level_ || type < 0 || type >= kTypes)
        return 0;
    const DecalType& kind = kDecalTypes[static_cast<std::size_t>(type)];
    const Renderer::Texture* texture = textures_ ? textures_->texture(kind.texture) : nullptr;
    if (!texture)
        return 0;
    if (size < 0.0f)
        size = draw(kind.minSize, kind.maxSize, rng);
    if (size <= 0.0f)
        return 0;
    if (rotation < 0.0f)
        rotation = draw(0.0f, 359.0f, rng);

    // The square's axes in the plane facing `normal`, turned by the rotation.
    const Math::Vector3 n = normal.length() > 1e-5f ? normal.normalized() : Math::Vector3{0.0f, 1.0f, 0.0f};
    const Math::Vector3 helper = std::fabs(n.y) < 0.9f ? Math::Vector3{0.0f, 1.0f, 0.0f} : Math::Vector3{1.0f, 0.0f, 0.0f};
    const Math::Vector3 u0 = helper.cross(n).normalized(), v0 = n.cross(u0);
    const float32 r = rotation * (3.14159265f / 180.0f);
    const Math::Vector3 u = u0 * std::cos(r) + v0 * std::sin(r), v = n.cross(u);
    const float32 half = size * 0.5f;

    auto mesh = std::make_unique<Renderer::Mesh>();
    const auto& tris = level_->collisionTriangles();
    const auto& light = level_->collisionLight();
    for (std::size_t i = 0; i < tris.size(); ++i)
    {
        const auto& t = tris[i];
        // The box of half the size about the point (0x4829b0: size x 0.5 each way).
        auto outside = [&](float32 Math::Vector3::*axis) {
            const float32 lo = at.*axis - half, hi = at.*axis + half;
            return (t.a.*axis < lo && t.b.*axis < lo && t.c.*axis < lo) || (t.a.*axis > hi && t.b.*axis > hi && t.c.*axis > hi);
        };
        if (outside(&Math::Vector3::x) || outside(&Math::Vector3::y) || outside(&Math::Vector3::z))
            continue;
        Math::Vector3 tn = (t.b - t.a).cross(t.c - t.a);
        if (tn.length() < 1e-8f)
            continue;
        tn = tn.normalized();
        if (tn.dot(n) < 0.0f)
            tn = tn * -1.0f; // collision triangles are double-sided
        if (tn.dot(n) < kFacing)
            continue;
        const Math::Vector4 white{1.0f, 1.0f, 1.0f, 1.0f};
        std::vector<ClipVertex> poly{{t.a, i < light.size() ? light[i][0] : white},
                                     {t.b, i < light.size() ? light[i][1] : white},
                                     {t.c, i < light.size() ? light[i][2] : white}};
        poly = clip(poly, at, u, half);
        poly = clip(poly, at, u * -1.0f, half);
        poly = clip(poly, at, v, half);
        poly = clip(poly, at, v * -1.0f, half);
        if (poly.size() < 3)
            continue;
        const uint32 first = mesh->vertexCount();
        for (const ClipVertex& c : poly)
        {
            const Math::Vector3 d = c.p - at;
            mesh->addVertex(d + tn * kLift, tn, {d.dot(u) / size + 0.5f, 0.5f - d.dot(v) / size},
                            {c.light.x, c.light.y, c.light.z, 1.0f});
        }
        for (uint32 k = 1; k + 1 < poly.size(); ++k)
            mesh->addTriangle(first, first + k, first + k + 1);
    }
    if (mesh->triangleCount() == 0)
        return 0;
    mesh->computeBoundingBox();
    Renderer::Material m = Renderer::Material::defaultMaterial();
    m.setFlag(Engine::RenderMode::BackCull, false);
    m.setFlag(Engine::RenderMode::BlendOn, true);
    m.setTexture(*texture);
    m.setDiffuse(Math::Vector4{1.0f, 1.0f, 1.0f, static_cast<float32>(kind.alpha) / 255.0f});
    mesh->setMaterial(m);
    mesh->setLighting(Renderer::MeshLighting::Baked);
    mesh->setCastsShadow(false);
    mesh->setTransform(Math::Matrix4x4::translation(at));

    if (ring_.size() >= kRing)
    {
        drop(ring_.front());
        ring_.pop_front();
    }
    Decal d;
    d.id = nextId_++;
    d.mesh = mesh.get();
    d.at = at;
    d.u = u;
    d.v = v;
    scene_->addEntity(std::move(mesh));
    ring_.push_back(d);
    return d.id;
}

bool Decals::setScale(uint32 id, float32 scale)
{
    for (const Decal& d : ring_)
        if (d.id == id)
        {
            // Scaled within its plane about its centre.
            const float32 k = scale - 1.0f;
            const float32 u[3] = {d.u.x, d.u.y, d.u.z}, v[3] = {d.v.x, d.v.y, d.v.z};
            float32 s[3][3];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c)
                    s[r][c] = (r == c ? 1.0f : 0.0f) + k * (u[r] * u[c] + v[r] * v[c]);
            Math::Matrix4x4 t = Math::Matrix4x4::identity();
            t.m00 = s[0][0]; t.m01 = s[0][1]; t.m02 = s[0][2];
            t.m10 = s[1][0]; t.m11 = s[1][1]; t.m12 = s[1][2];
            t.m20 = s[2][0]; t.m21 = s[2][1]; t.m22 = s[2][2];
            t.m03 = d.at.x; t.m13 = d.at.y; t.m23 = d.at.z;
            d.mesh->setTransform(t);
            return true;
        }
    return false;
}

void Decals::drop(const Decal& d)
{
    if (device_)
        device_->removeMesh(d.mesh);
    scene_->removeEntity(d.mesh->id());
}

} // namespace Game
