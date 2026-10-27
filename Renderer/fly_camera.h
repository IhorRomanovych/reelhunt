// Renderer/fly_camera.h - input-driven free-fly camera controller (WASD + hold-LMB look).
#pragma once
#include "engine_types.h"

#include <SDL3/SDL.h>

#include "Math/vector.h"
#include "Renderer/camera.h"

namespace Renderer {

// Free-fly debug camera: WASD move, Space/E up, C/Q down, Shift boost, hold-LMB to look. Owns the
// position/yaw/pitch state and drives a Camera via setView/update each frame. Kept separate from
// the pure Camera transform class so that one stays free of SDL input handling.
class FlyCamera {
public:
    // Feed an SDL event; handles hold-LMB mouse-look (yaw/pitch with pitch clamp). Ignores others.
    void handleEvent(const SDL_Event &evt);

    // Per-frame: apply keyboard movement, then drive `cam` via setView/update and log changes.
    void update(float32 dt, Camera *cam);

    // Jump to a pose (yaw/pitch in radians), e.g. a reproducible debug vantage.
    void setPose(const Math::Vector3 &pos, float32 yaw, float32 pitch) noexcept
    {
        pos_ = pos;
        yaw_ = yaw;
        pitch_ = pitch;
    }

    [[nodiscard]] float32 yaw() const noexcept { return yaw_; }

private:
    Math::Vector3 pos_{2.0f, 22.0f, 38.0f}; // Front-on full-body view centered on bun_pc's animated bounding box (~x[-2,6] y[18,26]). Orbit with LMB drag / WASD.
    float32 yaw_ = 0.0f;
    float32 pitch_ = 0.0f; // look straight ahead (-Z) at the character
    bool mouseLook_ = false;

    // Last-logged transform (so a known view can be reproduced in automated screenshots).
    Math::Vector3 lastPos_{0.0f, 0.0f, 0.0f};
    float32 lastYaw_ = 1e9f;
    float32 lastPitch_ = 1e9f;
};

} // namespace Renderer
