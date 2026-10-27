// Game/locomotion.h - which player clip (PlayerAnims block of allanims.ifp) shows a movement state.
//
// The PC game's actions are FORWARD / BACK / STRAFE LEFT / STRAFE RIGHT, RUN and SNEAK (GXT
// MFWD MBACK MLEFT MRIGHT SPRINT SNEAK); there is no jump. The player faces the camera and
// strafes, so each gait has four directional clips:
//   standing   Stand                          sneaking   Sneak_Stand
//   moving     Run_<dir>                      sneak move Sneak_Walk_<dir>
//   RUN held   Sprint_<dir>                   + RUN      Sneak_Run_<dir>
// <dir> is Fwd / Bkw / Left / Right from the dominant input axis. The keyboard has no analog
// "walk" (Walk_<dir> covers the same ground as Run_<dir> and is left for a gamepad's half-stick).
#pragma once
#include "engine_types.h"

#include <string>

#include "Game/player_controller.h"

namespace Game {

struct LocomotionInput {
    PlayerState state = PlayerState::Idle;
    float32 moveX = 0.0f; // +1 right
    float32 moveZ = 0.0f; // +1 forward
    bool run = false;
};

// The looping clip for this frame's movement.
[[nodiscard]] std::string locomotionClip(const LocomotionInput& in);

// Moving diagonally (both axes held), the other axis's clip of the same gait and how much of it
// to blend in: |minor| / (|major| + |minor|), 0.5 on a keyboard diagonal (ours: the PC game's
// clips are the four directions only). Empty when moving straight or not moving.
struct LocomotionMix {
    std::string clip;
    float32 weight = 0.0f;
};
[[nodiscard]] LocomotionMix locomotionMix(const LocomotionInput& in);

// The clip whose root motion sets each gait's speed (the forward variant).
inline constexpr const char* kStandClip = "Stand";
inline constexpr const char* kMoveClip = "Run_Fwd";
inline constexpr const char* kSprintClip = "Sprint_Fwd";
inline constexpr const char* kSneakMoveClip = "Sneak_Walk_Fwd";
inline constexpr const char* kSneakRunClip = "Sneak_Run_Fwd";

} // namespace Game
