// Game/player_controller.h - G1 player movement + third-person follow camera (pure math).
//
// The controller owns the player's position/velocity/look and the follow camera, and advances
// them with camera-relative WASD movement, crouch, jump and gravity. It is deliberately free of
// SDL / Vulkan / Physics so the movement + camera math is unit-testable; the caller owns the
// ground query (onGround) and the collision clamp (it reads position(), pushes the body back,
// then feeds the clamped position back in via setPosition()).
#pragma once

#include "engine_types.h"
#include "Math/math.h"
#include "Math/vector.h"
#include "Math/quaternion.h"
#include "Math/matrix.h"

namespace Game
{
    // The player's movement/interaction state (the G1 state machine).
    enum class PlayerState : std::uint8_t
    {
        Idle = 0,
        Walk,
        Run,
        Crouch,      // crouched, stationary
        CrouchMove,  // crouched, moving
        Jump,        // ascending
        Fall,        // airborne, descending
        Dead,
    };

    // One frame of camera-relative player input. Decoupled from SDL so the controller is
    // unit-testable. moveX/moveZ are -1..+1 (D/A and W/S); mouseDX/mouseDY are look deltas.
    struct PlayerInput
    {
        float32 moveX = 0.0f;   // +1 right (D), -1 left (A)
        float32 moveZ = 0.0f;   // +1 forward (W), -1 back (S)
        float32 mouseDX = 0.0f; // look deltas (pixels)
        float32 mouseDY = 0.0f;
        bool crouch = false;    // sneak (held, e.g. C / Ctrl)
        bool jump = false;      // edge, this frame (e.g. Space)
        bool run = false;       // RUN held (e.g. Shift); while sneaking, a sneak-run
        float32 maxSpeed = 0.0f; // > 0: caps the ground speed (aiming a gun walks at Gun_Move_* pace)
    };

    // Tunable movement constants (meters/seconds). The four ground speeds default to the root
    // motion of the player's forward clips (Run_Fwd, Sprint_Fwd, Sneak_Walk_Fwd, Sneak_Run_Fwd in
    // allanims.ifp); the session re-reads them from the loaded clips. The rest are placeholders
    // until measured from the decompiled constants or the video.
    struct PlayerTuning
    {
        float32 walkSpeed = 6.09f;      // default movement (Run_Fwd)
        float32 runSpeed = 8.32f;       // RUN held (Sprint_Fwd)
        float32 crouchSpeed = 0.61f;    // sneaking (Sneak_Walk_Fwd)
        float32 crouchRunSpeed = 2.2f;  // sneaking + RUN (Sneak_Run_Fwd)
        float32 accel = 40.0f;      // ground horizontal accel (rate of approaching target speed)
        float32 airControl = 0.25f; // fraction of ground accel while airborne
        float32 gravity = 22.0f;    // m/s^2 (game feel; snappier than 9.81)
        float32 jumpSpeed = 6.0f;   // initial upward velocity
        float32 lookSens = 0.0022f;
        float32 maxPitch = 1.48353f; // 85 degrees (the camera's limits, 0x7a0fac +0x428 / +0x42c)
        // The follow camera (exe 0x58c020 / 0x58b820 on the camera object 0x7a0fac): it
        // looks at a pivot on him - (-0.3, 2.0, 0) in his frame (+0xa0: 0.3 m to his right, 2 m
        // over his feet; crouched 1.3 m) - from 3.9 m behind it (+0xcc: (0, 0, -3.9)), turned by
        // the pitch about his right and by his heading. The pivot's offset moves to a new one 0.13 m
        // a frame (0x58b820's step; ours at 30 frames a second).
        float32 camDist = 3.9f;     // distance from the pivot
        float32 camSide = 0.3f;     // the pivot to his right
        float32 standEye = 2.0f;    // the pivot's height standing
        float32 crouchEye = 1.3f;   // ... crouched (+0x3d4 0xd)
        float32 pivotStep = 3.9f;   // m/s the pivot's offset moves (0.13 m a frame)
        float32 feetBelow = 0.0f;   // his feet below the position the controller keeps (the body's centre)
    };

    // Pure movement + third-person camera math (no SDL/Vulkan/Physics -> unit-testable).
    class PlayerController
    {
    public:
        PlayerController() = default;

        void setTuning(const PlayerTuning& t) noexcept { tuning_ = t; }
        [[nodiscard]] const PlayerTuning& tuning() const noexcept { return tuning_; }

        // Reset to a spawn pose (position + look). Zeroes velocity, snaps the camera behind.
        void reset(const Math::Vector3& spawn, float32 yaw, float32 pitch) noexcept;

        // Advance one frame. `onGround` is the caller's ground query. Returns the new state.
        PlayerState update(float32 dt, const PlayerInput& in, bool onGround);

        [[nodiscard]] PlayerState state() const noexcept { return state_; }
        [[nodiscard]] Math::Vector3 position() const noexcept { return pos_; }
        [[nodiscard]] Math::Vector3 velocity() const noexcept { return vel_; }
        [[nodiscard]] float32 yaw() const noexcept { return yaw_; }
        [[nodiscard]] float32 pitch() const noexcept { return pitch_; }
        [[nodiscard]] bool isCrouching() const noexcept { return crouching_; }
        // The pivot's height over his feet now (moving toward the stand / crouch one).
        [[nodiscard]] float32 eyeHeight() const noexcept { return pivotHeight_; }
        // The character's world orientation (yaw about Y; forward = -Z at yaw 0).
        [[nodiscard]] Math::Quaternion orientation() const noexcept;

        // Turn the look (a gun's lock-on swings him round to its target).
        void setLook(float32 yaw, float32 pitch) noexcept { yaw_ = yaw; pitch_ = pitch; }

        // The caller feeds the collision-clamped position back in (after the physics pass).
        void setPosition(const Math::Vector3& p) noexcept { pos_ = p; }

        // The follow camera: camDist from the pivot back along the view (yaw and pitch), looking
        // at the pivot.
        [[nodiscard]] Math::Vector3 cameraPosition() const noexcept { return camPos_; }
        [[nodiscard]] Math::Vector3 cameraPivot() const noexcept;
        [[nodiscard]] float32 cameraYaw() const noexcept { return yaw_; }
        [[nodiscard]] float32 cameraPitch() const noexcept { return pitch_; }

        // The yaw-only ground basis (forward + right on the XZ plane, forward = -Z at yaw 0),
        // matching the renderer's rotY(-yaw) convention so WASD tracks where the camera looks.
        static void groundBasis(float32 yaw, Math::Vector3& fwd, Math::Vector3& rgt) noexcept;

    private:
        // The ideal (un-smoothed) camera position for the current pose.
        Math::Vector3 idealCameraPos() const noexcept;

        Math::Vector3 pos_{};
        Math::Vector3 vel_{};
        Math::Vector3 camPos_{};
        float32 pivotHeight_ = 2.0f;
        float32 yaw_ = 0.0f;
        float32 pitch_ = 0.0f;
        bool crouching_ = false;
        PlayerState state_ = PlayerState::Idle;
        PlayerTuning tuning_{};
    };
}
