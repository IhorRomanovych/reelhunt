// Game/ambient_light.h - a character's ambient light, as the exe works it out (0x49a6f0).
//
// Each frame the world's baked light under the character is sampled (0x479350; a channel above
// 1.0 is an invalid sample and the previous value stands) and the character's light moves an
// eighth of the way towards it (when it differs by more than 0.001), never below
// (0.08, 0.08, 0.12) - a faint blue so characters never go fully black. The exe steps once per
// frame; here the eighth is per 1/30 s so the easing does not depend on the frame rate.
#pragma once
#include "engine_types.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "Math/vector.h"

namespace Game {

class AmbientLight {
public:
    static constexpr float32 kStepFps = 30.0f;

    Math::Vector4 update(std::optional<Math::Vector3> sample, float32 dt)
    {
        if (sample && sample->x <= 1.0f && sample->y <= 1.0f && sample->z <= 1.0f)
        {
            if (!started_)
            {
                current_ = *sample;
                started_ = true;
            }
            else
            {
                const float32 k = 1.0f - std::pow(7.0f / 8.0f, std::max(dt, 0.0f) * kStepFps);
                auto ease = [k](float32& c, float32 s) {
                    if (std::fabs(s - c) > 0.001f)
                        c += (s - c) * k;
                };
                ease(current_.x, sample->x);
                ease(current_.y, sample->y);
                ease(current_.z, sample->z);
            }
        }
        current_.x = std::max(current_.x, 0.08f);
        current_.y = std::max(current_.y, 0.08f);
        current_.z = std::max(current_.z, 0.12f);
        return {current_.x, current_.y, current_.z, 1.0f};
    }

private:
    Math::Vector3 current_{0.08f, 0.08f, 0.12f};
    bool started_ = false;
};

} // namespace Game
