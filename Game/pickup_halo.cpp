// Game/pickup_halo.cpp - pickup halos. See pickup_halo.h.
#include "Game/pickup_halo.h"

#include <algorithm>
#include <cmath>

#include "Assets/ini_records.h"
#include "Math/math.h"
#include "Renderer/material.h"

namespace Game {

namespace {

constexpr float32 kNear = 4.0f, kNearFade = 3.4f; // 0x7cee14, 0x7cee14 - 0x7ce134
constexpr float32 kFar = 40.0f, kFarFade = 10.0f; // 0x7cee1c, 0x7cee20 - 0x7cee1c
constexpr float32 kReach = 50.0f;                 // 0x7cee20: beyond it nothing is queued

} // namespace

PickupHalo pickupHalo(int32 ct, const std::string& holsterSlot)
{
    PickupHalo h;
    h.shown = true;
    if (ct >= 8 && ct <= 10)
    {
        h.r = h.g = h.b = 80;
        h.pulses = true;
        return h;
    }
    if (ct >= 102 && ct <= 107)
    {
        h.r = 60;
        h.g = 30;
        h.size = 0.8f;
        return h;
    }
    struct Slot { const char* name; int32 r, g, b; };
    static constexpr Slot kSlots[] = {
        {"BACK", 80, 0, 0}, {"BELT_RIGHT", 0, 0, 80}, {"BELT_LEFT", 0, 80, 0}, {"BELT_REAR", 80, 80, 0}, {"SPECIAL", 20, 70, 50}};
    for (const Slot& s : kSlots)
        if (Assets::iequals(holsterSlot, s.name))
        {
            h.r = s.r;
            h.g = s.g;
            h.b = s.b;
            return h;
        }
    h.shown = false;
    return h;
}

PickupHalo savePointHalo()
{
    PickupHalo h;
    h.shown = true;
    h.r = 20;
    h.g = 80;
    h.b = 120;
    h.size = 1.5f; // 0x738fa0
    return h;
}

PickupHalo gasCanHalo()
{
    PickupHalo h;
    h.shown = true;
    h.r = 20;
    h.g = 70;
    h.b = 30;
    h.size = 0.9f; // 0x69b3dc
    return h;
}

std::unique_ptr<Renderer::Mesh> makePickupHalo(const Renderer::Texture* texture)
{
    auto mesh = std::make_unique<Renderer::Mesh>();
    const Math::Vector3 n{0.0f, 0.0f, 1.0f};
    mesh->addVertex({-0.5f, -0.5f, 0.0f}, n, {0.0f, 1.0f});
    mesh->addVertex({0.5f, -0.5f, 0.0f}, n, {1.0f, 1.0f});
    mesh->addVertex({0.5f, 0.5f, 0.0f}, n, {1.0f, 0.0f});
    mesh->addVertex({-0.5f, 0.5f, 0.0f}, n, {0.0f, 0.0f});
    mesh->addTriangle(0, 1, 2);
    mesh->addTriangle(0, 2, 3);
    mesh->computeBoundingBox();
    Renderer::Material m = Renderer::Material::defaultMaterial();
    m.setFlag(Engine::RenderMode::BackCull, false);
    m.setFlag(Engine::RenderMode::Additive, true);
    if (texture)
        m.setTexture(*texture);
    mesh->setMaterial(m);
    mesh->setLighting(Renderer::MeshLighting::Unlit);
    mesh->setCastsShadow(false);
    mesh->setVisible(false);
    return mesh;
}

void placePickupHalo(Renderer::Mesh& mesh, const PickupHalo& halo, const Math::Vector3& centre, const Math::Vector3& eye,
                     const Math::Vector3& right, const Math::Vector3& up, uint32 timeMs, std::mt19937& rng)
{
    const float32 d = (centre - eye).length();
    if (!halo.shown || d >= kReach)
    {
        mesh.setVisible(false);
        return;
    }
    // 0x5fb6d0: faded near the camera and far from it, each channel truncated (below 1: off).
    float32 k = 1.0f;
    if (d < kNear)
        k = (kNearFade - (kNear - d)) / kNearFade;
    else if (d > kFar)
        k = (kFarFade - (d - kFar)) / kFarFade;
    auto channel = [k](int32 c) {
        const float32 v = std::min(static_cast<float32>(c), static_cast<float32>(c) * k);
        return v < 1.0f ? 0.0f : std::trunc(v) / 255.0f;
    };
    const float32 alpha = (static_cast<float32>(std::uniform_int_distribution<int32>(0, 0x7ff)(rng)) / 2047.0f * 60.0f + 50.0f) / 255.0f;
    Renderer::Material m = mesh.material();
    m.setDiffuse(Math::Vector4{channel(halo.r), channel(halo.g), channel(halo.b), std::trunc(alpha * 255.0f) / 255.0f});
    mesh.setMaterial(m);

    // 0x4c7cc0: first aid breathes, 0.9 + 0.08 sin over 1024 ms.
    const float32 size =
        halo.pulses ? halo.size + 0.08f * std::sin(static_cast<float32>(timeMs & 0x3ffu) * (2.0f * Math::pi / 1024.0f)) : halo.size;
    // 0x5faf60: the camera's right and up, size across.
    const Math::Vector3 x = right * size, y = up * size, z = x.cross(y).normalized();
    Math::Matrix4x4 t = Math::Matrix4x4::identity();
    t.m00 = x.x; t.m10 = x.y; t.m20 = x.z;
    t.m01 = y.x; t.m11 = y.y; t.m21 = y.z;
    t.m02 = z.x; t.m12 = z.y; t.m22 = z.z;
    t.m03 = centre.x; t.m13 = centre.y; t.m23 = centre.z;
    mesh.setTransform(t);
    mesh.setVisible(true);
}

} // namespace Game
