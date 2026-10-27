// Game/locomotion.cpp - player movement state -> clip name. See locomotion.h.
#include "Game/locomotion.h"

#include <cmath>

namespace Game {

namespace {

const char* direction(float32 moveX, float32 moveZ)
{
    if (std::fabs(moveZ) >= std::fabs(moveX))
        return moveZ >= 0.0f ? "Fwd" : "Bkw";
    return moveX > 0.0f ? "Right" : "Left";
}

} // namespace

LocomotionMix locomotionMix(const LocomotionInput& in)
{
    const float32 ax = std::fabs(in.moveX), az = std::fabs(in.moveZ);
    if (ax < 0.05f || az < 0.05f)
        return {};
    // The dominant axis picks the main clip (locomotionClip); the other one is mixed in.
    const bool zMain = az >= ax;
    LocomotionInput other = in;
    if (zMain)
        other.moveZ = 0.0f;
    else
        other.moveX = 0.0f;
    const std::string clip = locomotionClip(other);
    if (clip == locomotionClip(in) || clip == kStandClip || clip == "Sneak_Stand")
        return {};
    return {clip, (zMain ? ax : az) / (ax + az)};
}

std::string locomotionClip(const LocomotionInput& in)
{
    const std::string dir = direction(in.moveX, in.moveZ);
    switch (in.state)
    {
    case PlayerState::Walk:
        return "Run_" + dir;
    case PlayerState::Run:
        return "Sprint_" + dir;
    case PlayerState::Crouch:
        return "Sneak_Stand";
    case PlayerState::CrouchMove:
        return (in.run ? "Sneak_Run_" : "Sneak_Walk_") + dir;
    case PlayerState::Jump:
    case PlayerState::Fall:
        return "Fall_Flail_Upright";
    case PlayerState::Idle:
    case PlayerState::Dead:
        break;
    }
    return kStandClip;
}

} // namespace Game
