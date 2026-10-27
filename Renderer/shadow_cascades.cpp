// Renderer/shadow_cascades.cpp - cascaded shadow map fitting. See shadow_cascades.h.
#include "Renderer/shadow_cascades.h"

#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Renderer {

CascadeFit fitShadowCascades(const CascadeCamera& camera, float32 nearPlane, const float32* splits, uint32 splitCount,
                             const glm::vec3& lightDir, uint32 mapSize, float32 casterMargin)
{
    CascadeFit fit;
    fit.count = std::min(splitCount, kMaxShadowCascades);
    const glm::mat4 viewToWorld = glm::inverse(camera.view);
    const float32 tanY = std::tan(glm::radians(camera.fovYDegrees) * 0.5f);
    const float32 tanX = tanY * camera.aspect;

    const glm::vec3 dir = glm::normalize(lightDir);
    const glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    // Rotation-only light view, used to snap the cascade centre to the texel grid.
    const glm::mat4 lightRot = glm::lookAt(glm::vec3(0.0f), dir, up);

    float32 sliceNear = nearPlane;
    for (uint32 i = 0; i < fit.count; ++i)
    {
        const float32 sliceFar = splits[i];

        // The slice's eight corners in world space (view space looks down -Z).
        glm::vec3 corners[8];
        uint32 k = 0;
        for (float32 d : {sliceNear, sliceFar})
            for (float32 sy : {-1.0f, 1.0f})
                for (float32 sx : {-1.0f, 1.0f})
                    corners[k++] = glm::vec3(viewToWorld * glm::vec4(sx * d * tanX, sy * d * tanY, -d, 1.0f));

        // Bounding sphere: constant size for a given slice whatever the camera's orientation.
        glm::vec3 center(0.0f);
        for (const glm::vec3& c : corners)
            center += c;
        center /= 8.0f;
        float32 radius = 0.0f;
        for (const glm::vec3& c : corners)
            radius = std::max(radius, glm::length(c - center));
        radius = std::ceil(radius * 16.0f) / 16.0f;

        // Snap the centre to whole texels in light space.
        const float32 texel = 2.0f * radius / static_cast<float32>(mapSize);
        glm::vec4 ls = lightRot * glm::vec4(center, 1.0f);
        ls.x = std::floor(ls.x / texel) * texel;
        ls.y = std::floor(ls.y / texel) * texel;
        center = glm::vec3(glm::inverse(lightRot) * ls);

        const glm::vec3 eye = center - dir * (radius + casterMargin);
        const glm::mat4 view = glm::lookAt(eye, center, up);
        const glm::mat4 proj = glm::orthoRH_ZO(-radius, radius, -radius, radius, 0.0f, 2.0f * radius + casterMargin);

        ShadowCascade& c = fit.cascades[i];
        c.viewProj = proj * view;
        c.splitFar = sliceFar;
        c.texelWorld = texel;
        sliceNear = sliceFar;
    }
    return fit;
}

} // namespace Renderer
