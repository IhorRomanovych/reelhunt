// Renderer/lighting.h - how a scene is lit.
#pragma once
#include "engine_types.h"

#include "Math/vector.h"

namespace Renderer {

enum class LightingMode : uint32 {
    // The original look: world and props show their baked prelight (the level's lighting, e.g.
    // night for Jury_Turf); characters get only their ambient (the light under them). The
    // original's lamp posts emit no runtime light (Light_Inst radius 0).
    Baked = 0,
    // Debug daylight: a sun with cascaded shadows and a flat ambient; baked light is ignored.
    Midday = 1,
    // Baked light plus dynamic lamps: the prelight stays, the lamps light characters and cast
    // shadows; on baked surfaces a character's shadow removes the lamp's share of the light.
    Mixed = 2,
    // Fully dynamic: prelight ignored; moon (cascaded shadows) + lamps (shadowed) + a dim ambient.
    Dynamic = 3,
};

[[nodiscard]] inline const char* lightingModeName(LightingMode m) noexcept
{
    switch (m)
    {
    case LightingMode::Baked: return "original";
    case LightingMode::Midday: return "midday";
    case LightingMode::Mixed: return "mixed";
    case LightingMode::Dynamic: return "dynamic";
    }
    return "?";
}

struct SceneLighting {
    LightingMode mode = LightingMode::Midday;

    // Sun (Midday) or moon (Dynamic). The direction points away from it (the way light travels).
    Math::Vector3 sunDirection{-0.2f, -1.0f, -0.4f};
    Math::Vector3 sunColor{1.0f, 0.95f, 0.8f};
    float32 sunIntensity = 4.0f;
    bool shadows = true;

    // Ambient light for dynamically lit surfaces without a per-object override (Midday: the
    // flat sky term; Baked: what a character gets away from any light).
    Math::Vector3 ambient{0.5f, 0.5f, 0.5f};

    // Baked mode: scale on the prelight colours (1 = the stored values, as the original
    // modulates texture by vertex colour).
    float32 bakedScale = 1.0f;

    // Scale on every local light (lamps), and the darkest a baked surface may get when a
    // character's shadow removes a lamp's light from it (a fraction of its prelight).
    float32 lampScale = 1.0f;
    float32 bakedShadowFloor = 0.35f;

    // The level's sky (background) colour and its linear fog: surfaces fade into the sky colour
    // between fogStart and fogEnd metres of view depth (as RenderWare's linear camera fog).
    // Not applied in Midday (debug daylight).
    Math::Vector3 skyColor{0.008f, 0.01f, 0.02f};
    bool fog = false;
    float32 fogStart = 100.0f;
    float32 fogEnd = 120.0f;
};

} // namespace Renderer
