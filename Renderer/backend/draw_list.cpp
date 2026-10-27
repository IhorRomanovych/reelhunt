// Renderer/backend/draw_list.cpp - scene -> draws. See draw_list.h.
#include "Renderer/backend/draw_list.h"

#include "Renderer/backend/resource_cache.h"
#include "Renderer/light.h"
#include "Renderer/mesh.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include <glm/gtc/matrix_transform.hpp>

namespace Renderer::Backend {

namespace {

constexpr float32 kDefaultRoughness = 0.85f;

glm::vec3 toGlm(const Math::Vector3& v) { return {v.x, v.y, v.z}; }
glm::vec4 toGlm(const Math::Vector4& v) { return {v.x, v.y, v.z, v.w}; }

// The shadow camera of a spot light: a perspective frustum along the cone, out to its range.
glm::mat4 spotShadowMatrix(const Light& light)
{
    const glm::vec3 pos = toGlm(light.position());
    glm::vec3 dir = toGlm(light.direction());
    dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec3(0.0f, -1.0f, 0.0f);
    const glm::vec3 up = std::fabs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    const float32 fov = std::min(2.0f * light.coneOuter() + 0.1f, glm::radians(170.0f));
    return glm::perspectiveRH_ZO(fov, 1.0f, 0.1f, std::max(light.radius(), 0.2f)) * glm::lookAtRH(pos, pos + dir, up);
}

void mix(uint64& h, const void* data, std::size_t n)
{
    const auto* b = static_cast<const uint8*>(data);
    for (std::size_t i = 0; i < n; ++i)
        h = (h ^ b[i]) * 1099511628211ull;
}

} // namespace

void DrawList::build(const Scene& scene, ResourceCache& cache)
{
    items.clear();
    bones.clear();
    lights.clear();
    shadowLights = 0u;
    shadowSignature = 1469598103934665603ull;
    uint32 staticCasters = 0u;
    std::unordered_map<const SkinPose*, uint32> poseOffsets;
    // Under a sun or moon the real shadows replace the ones painted into the level.
    const LightingMode mode = scene.lighting().mode;
    const bool skyShadows = (mode == LightingMode::Midday || mode == LightingMode::Dynamic) && scene.lighting().shadows;

    for (const auto& e : scene.entities())
    {
        if (const auto* light = dynamic_cast<const Light*>(e.get()))
        {
            if (light->intensity() <= 0.0f)
                continue;
            GpuLight g;
            g.positionRadius = glm::vec4(toGlm(light->position()), light->radius());
            g.colorIntensity = glm::vec4(glm::vec3(toGlm(light->color())), light->intensity());
            const bool spot = light->lightType() == LightType::Spot;
            glm::vec3 dir = toGlm(light->direction());
            dir = glm::length(dir) > 1e-6f ? glm::normalize(dir) : glm::vec3(0.0f, -1.0f, 0.0f);
            g.directionCos = glm::vec4(dir, spot ? std::cos(light->coneOuter()) : -2.0f);
            g.params = glm::vec4(spot ? std::cos(light->coneInner()) : 1.0f, -1.0f, static_cast<float32>(light->rwFlags()), 0.0f);
            if (spot && light->castingShadow() && shadowLights < kMaxShadowLights && scene.lighting().shadows)
            {
                g.params.y = static_cast<float32>(shadowLights++);
                g.shadowViewProj = spotShadowMatrix(*light);
                mix(shadowSignature, &g.shadowViewProj, sizeof(g.shadowViewProj));
            }
            lights.push_back(g);
            continue;
        }
        const auto* mesh = dynamic_cast<const Mesh*>(e.get());
        if (!mesh || !mesh->visible() || mesh->triangleCount() == 0u || (skyShadows && mesh->paintedShadow()))
            continue;

        DrawItem item;
        item.mesh = &cache.mesh(*mesh);
        item.skinned = item.mesh->skinned;
        item.castsShadow = mesh->castsShadow();
        item.moving = mesh->movingCaster();
        const Material& mat = mesh->material();
        item.additive = mat.isFlag(Engine::RenderMode::Additive);
        item.translucent = mat.isFlag(Engine::RenderMode::BlendOn) || item.additive;
        item.material = cache.materialSet(mat.hasTexture() ? mat.texture() : nullptr);
        if (item.castsShadow && !item.moving)
            ++staticCasters;

        DrawConstants& c = item.constants;
        // z: alpha test - cut-outs everywhere; blended surfaces only where it decides shadows.
        const bool alphaTest = mat.isFlag(Engine::RenderMode::AlphaTest) || item.translucent;
        // w: 0 lit, 1 baked (vertex colour light), 2 unlit (forward pass only).
        const float32 lighting = mesh->lighting() == MeshLighting::Unlit ? 2.0f : mesh->lighting() == MeshLighting::Baked ? 1.0f : 0.0f;
        c.material = glm::vec4(0.0f, kDefaultRoughness, alphaTest ? 1.0f : 0.0f, lighting);
        c.tint = toGlm(mat.diffuse()); // alpha: an unlit surface's opacity
        c.ambient = toGlm(mesh->ambientLight());

        if (item.skinned)
        {
            // Skinned vertices are placed by their pose; a mesh without one stays in bind pose.
            const SkinPose* pose = mesh->skinPose();
            if (pose && pose->boneCount() > 0u)
            {
                auto [it, added] = poseOffsets.try_emplace(pose, static_cast<uint32>(bones.size() / 16u));
                if (added)
                    bones.insert(bones.end(), pose->matrices.begin(), pose->matrices.end());
                c.boneOffset = it->second;
            }
            else
            {
                c.boneOffset = static_cast<uint32>(bones.size() / 16u);
                const glm::mat4 identity(1.0f);
                for (uint32 i = 0; i < kMaxSkinBones; ++i)
                    bones.insert(bones.end(), &identity[0][0], &identity[0][0] + 16);
            }
        }
        else
        {
            c.model = mesh->transform().toGlm();
            const BoundingBox& bb = mesh->boundingBox();
            if (!bb.empty())
            {
                const glm::vec3 lo = toGlm(bb.min), hi = toGlm(bb.max);
                item.center = glm::vec3(c.model * glm::vec4((lo + hi) * 0.5f, 1.0f));
                // Scale-safe radius: the largest axis scale of the model matrix.
                const float32 s = std::max({glm::length(glm::vec3(c.model[0])), glm::length(glm::vec3(c.model[1])),
                                            glm::length(glm::vec3(c.model[2]))});
                item.radius = glm::length(hi - lo) * 0.5f * s;
            }
        }
        items.push_back(item);
    }
    mix(shadowSignature, &staticCasters, sizeof(staticCasters));
    mix(shadowSignature, &shadowLights, sizeof(shadowLights));
}

Frustum::Frustum(const glm::mat4& m)
{
    // Gribb-Hartmann on the rows of the matrix; Vulkan clip depth is 0..w.
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    planes_ = {r3 + r0, r3 - r0, r3 + r1, r3 - r1, r2, r3 - r2};
    for (glm::vec4& p : planes_)
        p /= glm::length(glm::vec3(p));
}

bool Frustum::visible(const DrawItem& item) const noexcept
{
    if (item.radius < 0.0f)
        return true;
    for (const glm::vec4& p : planes_)
        if (glm::dot(glm::vec3(p), item.center) + p.w < -item.radius)
            return false;
    return true;
}

} // namespace Renderer::Backend
