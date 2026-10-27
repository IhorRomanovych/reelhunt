// Renderer/screen_effects.h - full-screen effects the game asks of the final composite: fade,
// flash, vignette and the camcorder look of the game's cut-scenes (white-noise bursts at camera
// cuts, grain, scanlines).
#pragma once
#include "engine_types.h"

#include "Math/vector.h"

namespace Renderer {

struct ScreenEffects {
    Math::Vector4 fade{0.0f, 0.0f, 0.0f, 0.0f};  // rgb, a = opacity (0 = none, 1 = fully faded)
    Math::Vector4 flash{0.0f, 0.0f, 0.0f, 0.0f}; // rgb, a = intensity
    float32 vignette = 0.0f;  // 0..1
    float32 noise = 0.0f;     // white noise (video static) 0..1
    float32 grain = 0.0f;     // film grain 0..1
    float32 scanlines = 0.0f; // interlace lines 0..1
    float32 time = 0.0f;      // seconds; animates the noise
    // The brightness setting's gamma: every colour (scene and 2D overlay) goes to the power of
    // this (exe 0x5db0d0 sets the device gamma ramp; Game/settings.h).
    float32 gamma = 1.0f;
};

} // namespace Renderer
